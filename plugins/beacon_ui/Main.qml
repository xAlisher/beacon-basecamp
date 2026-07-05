import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Logos.Theme
import Logos.Controls

Item {
    id: root

    // ── Palette → Logos design-system tokens (Theme.palette.*) ─────────────────
    readonly property color bgPrimary:     Theme.palette.background
    readonly property color bgSecondary:   Theme.palette.backgroundSecondary
    readonly property color bgActive:      Theme.palette.backgroundElevated
    readonly property color textPrimary:   Theme.palette.text
    readonly property color textSecondary: Theme.palette.textSecondary
    readonly property color textMuted:     Theme.palette.textMuted
    readonly property color accentOrange:  Theme.palette.primary
    readonly property color accentHover:   Theme.palette.primaryHover
    readonly property color accentPressed: Theme.palette.primaryPressed
    readonly property color successGreen:  Theme.palette.success
    readonly property color errorRed:      Theme.palette.error
    readonly property color warningYellow: Theme.palette.warning
    readonly property color borderColor:   Theme.palette.borderHairline

    // Mono font for hashes / channel ids / inscription ids
    readonly property string monoFont: "monospace"

    // ── State ─────────────────────────────────────────────────────────────────
    property string channelId:         ""
    property string nodeUrl:           "http://127.0.0.1:8080"
    property string explorerUrl:       "https://logosblocks.noders.services"
    property string signingKeyHex:     ""
    property string persistencePath:   ""
    property bool   watchStash:        true   // always on; no UI toggle
    property var    watchedSources:    []  // per-source whitelist for stash auto-inscription
    property bool   zoneSeqReady:      false
    property bool   settingsPanelOpen: false

    property bool   pollBusy:          false
    property var    manifestedModules: ({})   // module name → true, loaded from manifest-log.json
    property int    inscribedCount:    0
    property string channelLabel:      "My Beacon"   // kept for future use
    property string broadcastStatus:   ""             // kept for future use

    // ── Inscription lifecycle state ───────────────────────────────────────────
    property int  currentLibSlot:      0
    property var  pendingFinalizations: []
    property bool finalizationBusy:    false

    // Free-text inscribe runs deferred: the click clears the input, flips the
    // button to keycard-style dots and logs the request, THEN a short timer fires
    // the blocking publish — without the deferral nothing repaints until the IPC
    // call returns and the button feels dead.
    property bool   inscribeBusy:         false
    property string _pendingInscribeText: ""
    Timer {
        id: inscribeKick
        interval: 80
        repeat: false
        onTriggered: {
            var t = root._pendingInscribeText
            root._pendingInscribeText = ""
            // inscribeText is async now (backend → modules().logos_beacon); resolve
            // the busy flag / text-restore in its completion callback.
            root.inscribeText(t, function (ok) {
                root.inscribeBusy = false
                if (!ok && typeof inscribeInput !== "undefined" && inscribeInput.text.length === 0)
                    inscribeInput.text = t   // busy/failed before publish — give the text back
            })
        }
    }

    // ── Screen state ──────────────────────────────────────────────────────────
    property string currentScreen: "landing"   // "landing" | "main"

    // ── Keycard auth state ────────────────────────────────────────────────────
    property string keycardAuthId:     ""
    property string keycardAuthStatus: ""
    property bool   keycardConnected:  false
    property bool   authInFlight:      false   // guard against concurrent auth calls

    // ── logos_beacon bridge (v0.2) ────────────────────────────────────────────
    // logos_beacon (and its zone_sequencer hop) are Qt-free universal modules;
    // legacy logos.callModule can't reach them (returns "null" — beacon#30). We
    // call them through beacon_ui's own C++ QtRO backend, which forwards to
    // modules().logos_beacon.*, via logos.module("beacon_ui") + logos.watch().
    readonly property var beacon: (typeof logos !== "undefined" && logos.module)
                                  ? logos.module("beacon_ui") : null
    property bool beaconReady:  false   // backend QtRO context wired (isViewModuleReady)
    property bool _initialized: false   // one-shot init guard

    // ── Per-module channels cache ─────────────────────────────────────────────
    property var moduleChannels: ({})

    // ── Clipboard helper (root level; opacity:0 not visible:false) ───────────
    TextEdit {
        id: clipHelper
        opacity: 0; width: 0; height: 0
    }

    function copyToClipboard(text) {
        clipHelper.text = text
        clipHelper.selectAll()
        clipHelper.copy()
    }

    // ── callModuleParse — three-layer canonical form ──────────────────────────
    function callModuleParse(raw) {
        try {
            var tmp = JSON.parse(raw)
            if (typeof tmp === 'string') {
                try { return JSON.parse(tmp) } catch(e) { return tmp }
            }
            return tmp
        } catch(e) { return null }
    }

    // A seq* backend result (seqDeriveChannel / seqPublish) is either the plain
    // value string (channel id / tx hash) on success, or a {"error": "..."} JSON
    // string on failure — errors resolve through logos.watch's success callback,
    // not its reject path (the bridge only rejects on QtRO transport failure).
    // Returns the value string, or "" on any error.
    function seqResult(raw) {
        if (raw === undefined || raw === null) return ""
        var s = String(raw)
        if (s.length === 0) return ""
        var p = callModuleParse(raw)                       // null for a bare hex string
        if (p && typeof p === 'object' && p.error) return ""
        if (s.toLowerCase().indexOf("error") === 0) return ""
        return s
    }
    function seqIsError(raw) { return seqResult(raw).length === 0 }

    // The human-readable error from a seq* result ({"error":"..."} or an "error…"
    // string) so failures are surfaced to the user, not swallowed.
    function seqError(raw) {
        var p = callModuleParse(raw)
        if (p && typeof p === 'object' && p.error) return String(p.error)
        var s = String(raw === undefined || raw === null ? "" : raw)
        return s.length ? s : "unknown error"
    }

    // ── Unified feed helpers ──────────────────────────────────────────────────
    function feedEntryRow(cid, label, source, tsStr, inscriptionId, status, slotFrom, libAtSubmit) {
        return { rowType: "entry", cid: cid, label: label, source: source, tsStr: tsStr,
                 inscriptionId: inscriptionId, status: status,
                 slotFrom: slotFrom, libAtSubmit: libAtSubmit, msg: "", level: "" }
    }

    // feed index of the entry row for this cid, -1 if absent
    function feedRowFor(cid) {
        for (var i = 0; i < logModel.count; i++) {
            var r = logModel.get(i)
            if (r.rowType === "entry" && r.cid === cid) return i
        }
        return -1
    }

    function appendActivity(msg, level) {
        // cap activity rows only — entry rows must survive (finalization updates
        // find them by cid)
        var activityCount = 0, oldest = -1
        for (var i = 0; i < logModel.count; i++) {
            if (logModel.get(i).rowType === "activity") {
                activityCount++
                if (oldest < 0) oldest = i
            }
        }
        if (activityCount >= 200 && oldest >= 0) logModel.remove(oldest)
        logModel.append({ rowType: "activity",
                          tsStr: Qt.formatDateTime(new Date(), "HH:mm:ss"),
                          msg: msg, level: level || "info",
                          cid: "", label: "", source: "", inscriptionId: "",
                          status: "", slotFrom: 0, libAtSubmit: 0 })
    }

    // ── Keycard auth (deferred via Qt.callLater to unblock UI render) ─────────
    function requestKeycardAuth() {
        if (typeof logos === "undefined" || !logos.callModule) {
            root.authInFlight = false
            return
        }
        if (root.keycardAuthStatus === "pending" && root.keycardAuthId !== "") {
            root.authInFlight = false
            return
        }
        if (root.beacon)
            logos.watch(root.beacon.clearSigningKey(), function () {}, function () {})
        root.keycardConnected  = false
        root.keycardAuthStatus = ""
        var raw = logos.callModule("keycard", "requestAuth", ["bc:beacon", "logos_beacon"])
        var r = callModuleParse(raw)
        if (r && r.authId) {
            root.keycardAuthId     = r.authId
            root.keycardAuthStatus = "pending"
            keycardAuthPollTimer.start()
            // authInFlight stays true until poll completes/fails
        } else {
            root.keycardAuthStatus = "error"
            root.authInFlight = false
        }
    }

    // ── Zone sequencer setup (called once after Keycard auth) ─────────────────
    // Async: the backend derives the primary channel via seqDeriveChannel
    // (logos_beacon → modules().zone_sequencer) and returns the channel id.
    // done(ok) fires when it resolves.
    function configureZoneSeq(done) {
        if (!root.beacon || root.signingKeyHex === "") { if (done) done(false); return }
        logos.watch(root.beacon.ensureCheckpointsDir(), function () {}, function () {})
        // seqDeriveChannel is stateless (logos_beacon → modules().zone_sequencer);
        // the old set-key/node + get_channel_id + set-channel dance is gone.
        logos.watch(root.beacon.seqDeriveChannel(root.signingKeyHex),
            function (chRaw) {
                var ch = root.seqResult(chRaw)
                if (ch.length > 0) {
                    root.channelId    = ch
                    root.zoneSeqReady = true
                    if (done) done(true)
                } else {
                    appendActivity("zone sequencer configure failed: " + chRaw, "error")
                    if (done) done(false)
                }
            },
            function (e) {
                appendActivity("zone sequencer configure error: " + e, "error")
                if (done) done(false)
            })
    }

    // ── Channel manifest inscription ─────────────────────────────────────────
    // Inscribes a channel_manifest entry to the primary Beacon channel so Cord
    // can discover all module channels from a single Beacon key lookup.
    function inscribeManifest(name, channelId) {
        if (!root.beacon || !root.zoneSeqReady || root.signingKeyHex === "" || root.channelId === "") return

        // Stateless publish to the primary Beacon channel — seqPublish carries the
        // channel id + key explicitly, so the old set-key/set-channel re-init dance
        // (needed only because zone_sequencer used to be stateful) is gone.
        var payload = JSON.stringify({
            v:          1,
            type:       "channel_manifest",
            module:     name,
            channel_id: channelId,
            ts:         Math.floor(Date.now() / 1000)
        })

        logos.watch(root.beacon.seqPublish(root.nodeUrl, root.channelId, root.signingKeyHex, "", payload),
            function (pubRaw) {
                if (root.seqIsError(pubRaw)) {
                    appendActivity("manifest error for " + name, "error")
                    return
                }
                logos.watch(root.beacon.recordManifest(name), function () {}, function () {})
                var updated = {}
                for (var k in root.manifestedModules) updated[k] = root.manifestedModules[k]
                updated[name] = true
                root.manifestedModules = updated
                appendActivity("manifested " + name + " channel — " + channelId.substring(0,16) + "…", "success")
            },
            function (e) {
                appendActivity("manifest error for " + name + ": " + e, "error")
            })
    }

    // ── Per-module channel derivation ─────────────────────────────────────────
    // Async now: derives the module's signing key + channel id via the backend and
    // caches them. done(mc) fires with the cached { signingKey, channelId } entry,
    // or null on failure. seqDeriveChannel is stateless, so there's no primary-
    // channel state to save/restore anymore.
    function setupModuleChannel(name, done) {
        if (root.moduleChannels[name]) { if (done) done(root.moduleChannels[name]); return }
        if (!root.beacon) { if (done) done(null); return }

        logos.watch(root.beacon.deriveModuleSigningKey(name),
            function (raw) {
                var r = callModuleParse(raw)
                if (!r || !r.signingKey) { if (done) done(null); return }
                var sk = r.signingKey
                logos.watch(root.beacon.seqDeriveChannel(sk),
                    function (chRaw) {
                        var channelId = root.seqResult(chRaw)
                        if (channelId.length === 0) { if (done) done(null); return }
                        var updated = {}
                        var existing = root.moduleChannels
                        for (var key in existing) updated[key] = existing[key]
                        updated[name] = { signingKey: sk, channelId: channelId }
                        root.moduleChannels = updated
                        if (done) done(updated[name])
                    },
                    function (e) { if (done) done(null) })
            },
            function (e) { if (done) done(null) })
    }

    // ── Inscription flow ──────────────────────────────────────────────────────
    // rawPayload (optional): publish this exact string instead of a cid_pin JSON —
    // the free-text inscribe path for preparing test/campaign channels (#25)
    // Fully async now: every hop (getNodeInfo → setupModuleChannel → pinCid →
    // seqPublish → confirmInscription) goes through the backend via logos.watch.
    // done(ok) reports the outcome the old synchronous return used to carry.
    function inscribeCid(cid, label, source, rawPayload, done) {
        function finish(ok) { root.pollBusy = false; if (done) done(ok) }
        if (root.pollBusy) { if (done) done(false); return }
        if (!root.beacon)  { if (done) done(false); return }
        root.pollBusy = true

        // Capture node slot / lib_slot for slotFrom, libAtSubmit, and progress tracking
        logos.watch(root.beacon.getNodeInfo(), function (infoRaw) {
            var nodeSlot = 0
            var libSlot  = 0
            var info = callModuleParse(infoRaw)
            if (info && info.slot) {
                nodeSlot = info.slot
                libSlot  = info.lib_slot || 0
                root.currentLibSlot = libSlot
            }

            // Resolve the target channel/key, then run pin + publish.
            function afterChannel(useKey, useChannelId) {
                var useCheckpoint = ""
                logos.watch(root.beacon.pinCid(cid, label, source || "", nodeSlot, libSlot),
                    function (pinRaw) {
                        var pin = callModuleParse(pinRaw)
                        if (!pin || pin.error) {
                            appendActivity("error: pinCid " + (pin ? pin.error : "null"), "error")
                            finish(false); return
                        }
                        if (pin.duplicate === true) {
                            appendActivity("duplicate: " + cid.substring(0,16) + "…", "muted")
                            finish(true); return  // confirmed on-chain — caller should markInscribed
                        }

                        var entryIndex = pin.entryIndex
                        logModel.append(feedEntryRow(cid, label, source || "",
                            Qt.formatDateTime(new Date(), "HH:mm:ss"), "", "queued", nodeSlot, libSlot))

                        var payload = (rawPayload && rawPayload.length > 0) ? rawPayload : JSON.stringify({
                            v:      1,
                            type:   "cid_pin",
                            cid:    cid,
                            label:  label,
                            source: source || "",
                            ts:     Math.floor(Date.now() / 1000)
                        })

                        var pubStarted = Date.now()
                        // seqPublish is stateless (carries channel + key + node url),
                        // so primary and sub-channel publishes take the same path.
                        logos.watch(root.beacon.seqPublish(root.nodeUrl, useChannelId, useKey, useCheckpoint, payload),
                            function (pubRaw) {
                                var feedRow
                                if (root.seqIsError(pubRaw)) {
                                    // logos.watch delivers the ACTUAL backend result (not a
                                    // client-side timeout), so an error here is a real publish
                                    // failure — e.g. node unreachable → sequencer "timeout
                                    // waiting for sequencer ready". Surface the reason instead
                                    // of silently deferring to finalization.
                                    logos.watch(root.beacon.confirmInscription(entryIndex, "", "failed"), function () {}, function () {})
                                    feedRow = feedRowFor(cid)
                                    if (feedRow >= 0)
                                        logModel.setProperty(feedRow, "status", "failed")
                                    appendActivity("[" + (source || "primary") + "] publish failed: " + root.seqError(pubRaw), "error")
                                    finish(false); return
                                }

                                // Submitted — deferred confirmation via finalizationTimer
                                logos.watch(root.beacon.confirmInscription(entryIndex, "", "submitted"), function () {}, function () {})
                                feedRow = feedRowFor(cid)
                                if (feedRow >= 0)
                                    logModel.setProperty(feedRow, "status", "submitted")

                                var updatedPF = root.pendingFinalizations.slice()
                                updatedPF.push({
                                    entryIndex:  entryIndex,
                                    channelId:   useChannelId,
                                    slotFrom:    nodeSlot,
                                    libAtSubmit: libSlot,
                                    cid:         cid,
                                    // #44: enough to re-inscribe on non-inclusion (bootstrap
                                    // re-fetches the current tip, so a retry is a valid fresh op)
                                    useKey:      useKey,
                                    payload:     payload,
                                    source:      source || "",
                                    retries:     0
                                })
                                root.pendingFinalizations = updatedPF

                                appendActivity("[" + (source || "primary") + "] submitted — " + cid.substring(0,16) + "… awaiting finalization", "info")

                                // Manifest module channel to primary Beacon channel on first successful inscription
                                if (source && source.length > 0
                                        && root.moduleChannels[source]
                                        && !root.manifestedModules[source]) {
                                    inscribeManifest(source, root.moduleChannels[source].channelId)
                                }

                                finish(true)
                            },
                            function (e) {
                                logos.watch(root.beacon.confirmInscription(entryIndex, "", "failed"), function () {}, function () {})
                                var fr = feedRowFor(cid)
                                if (fr >= 0)
                                    logModel.setProperty(fr, "status", "failed")
                                appendActivity("[" + (source || "primary") + "] inscription error — " + e, "error")
                                finish(false)
                            })
                    },
                    function (e) { appendActivity("error: pinCid " + e, "error"); finish(false) })
            }

            if (source && source.length > 0) {
                setupModuleChannel(source, function (mc) {
                    if (mc) {
                        afterChannel(mc.signingKey, mc.channelId)
                    } else {
                        appendActivity("using primary channel for " + source, "info")
                        afterChannel(root.signingKeyHex, root.channelId)
                    }
                })
            } else {
                afterChannel(root.signingKeyHex, root.channelId)
            }
        }, function (e) { appendActivity("error: getNodeInfo " + e, "error"); finish(false) })
    }

    // ── Free-text inscribe (#25) — publishes the typed text verbatim to the
    // primary channel; pseudo-cid keys the feed row and stays unique so pinCid's
    // duplicate guard never trips. Used to prepare campaign/test channels.
    function inscribeText(text, done) {
        var t = text.trim()
        if (t.length === 0) { if (done) done(false); return }
        inscribeCid("text:" + Date.now(), t, "", t, done)
    }

    // ── Broadcast channel announce (kept for future use; not exposed in UI) ───
    function broadcastChannel() {
        if (root.pollBusy) return
        if (!root.beacon || !root.zoneSeqReady || root.channelId === "") return
        root.pollBusy = true
        root.broadcastStatus = ""

        logos.watch(root.beacon.setChannelLabel(root.channelLabel), function () {}, function () {})

        var payload = JSON.stringify({
            v:          1,
            type:       "channel_announce",
            module:     "logos_beacon",
            channel_id: root.channelId,
            label:      root.channelLabel,
            ts:         Math.floor(Date.now() / 1000)
        })

        logos.watch(root.beacon.seqPublish(root.nodeUrl, root.channelId, root.signingKeyHex, "", payload),
            function (pubRaw) {
                root.broadcastStatus = root.seqIsError(pubRaw) ? "error" : "ok"
                root.pollBusy = false
            },
            function (e) {
                root.broadcastStatus = "error"
                root.pollBusy = false
            })
    }

    // ── Module inscription queue polling ─────────────────────────────────────
    function pollModules() {
        if (root.pollBusy) return
        if (!root.keycardConnected) return
        if (typeof logos === "undefined") return
        if (root.watchedSources.length === 0) return

        // Gather queued entries across all watched sources, then inscribe one at a time.
        // keeper is now UNIVERSAL — callModule returns null (beacon#46), so it's fetched via the
        // backend (root.beacon.getKeeperInscriptionQueue, async). Legacy sources still use callModule.
        // Gathering is therefore async: collect all, then inscribe.
        var pending = []

        function addQueue(moduleName, queue) {
            if (!Array.isArray(queue)) return
            for (var j = 0; j < queue.length; j++) {
                var entry = queue[j]
                if (!entry || !entry.cid) continue
                pending.push({ moduleName: moduleName, cid: entry.cid, label: entry.label || entry.cid })
            }
        }

        function inscribeAll() {
            if (pending.length === 0) return
            function processNext(idx) {
                if (idx >= pending.length) return
                var p = pending[idx]
                appendActivity("queued from " + p.moduleName + ": " + p.cid.substring(0, 16) + "\u2026", "info")
                inscribeCid(p.cid, p.label, p.moduleName, "", function (inscribed) {
                    if (inscribed) {
                        if (p.moduleName === "keeper" && root.beacon)
                            logos.watch(root.beacon.markKeeperInscribed(p.cid), function () {}, function () {})
                        else if (logos.callModule)
                            logos.callModule(p.moduleName, "markInscribed", [p.cid])
                    }
                    processNext(idx + 1)
                })
            }
            processNext(0)
        }

        function gatherNext(i) {
            if (i >= root.watchedSources.length) { inscribeAll(); return }
            var moduleName = root.watchedSources[i]
            if (moduleName === "keeper" && root.beacon) {
                logos.watch(root.beacon.getKeeperInscriptionQueue(), function (raw) {
                    addQueue(moduleName, callModuleParse(raw)); gatherNext(i + 1)
                }, function () { gatherNext(i + 1) })
            } else if (logos.callModule) {
                addQueue(moduleName, callModuleParse(logos.callModule(moduleName, "getInscriptionQueue", []))); gatherNext(i + 1)
            } else {
                gatherNext(i + 1)
            }
        }
        gatherNext(0)
    }

    // ── Anchor tx resolution (module sub-channels) ────────────────────────────
    // ── Log refresh ───────────────────────────────────────────────────────────
    function refreshLog() {
        if (!root.beacon) return

        logos.watch(root.beacon.getInscriptionLog(), function (raw) {
            var entries = callModuleParse(raw)
            if (!Array.isArray(entries)) return

            // in-place sync keyed by cid — never clear/rebuild: activity rows and the
            // scroll position survive the 5s refresh
            var count = 0
            var seen = {}
            for (var i = 0; i < entries.length; i++) {
                var e  = entries[i]
                var cid = e.cid || ""
                seen[cid] = true
                var row = feedRowFor(cid)
                if (row < 0) {
                    logModel.append(feedEntryRow(cid, e.label || "", e.source || "",
                        Qt.formatDateTime(new Date(e.ts * 1000), "HH:mm:ss"),
                        e.inscriptionId || "", e.status || "pending",
                        e.slotFrom || 0, e.libAtSubmit || 0))
                } else {
                    var cur = logModel.get(row)
                    if (cur.status !== (e.status || "pending"))
                        logModel.setProperty(row, "status", e.status || "pending")
                    if (cur.inscriptionId !== (e.inscriptionId || ""))
                        logModel.setProperty(row, "inscriptionId", e.inscriptionId || "")
                    if (cur.label !== (e.label || ""))
                        logModel.setProperty(row, "label", e.label || "")
                }
                if (e.status === "ok" || e.status === "confirmed") count++
            }
            for (var j = logModel.count - 1; j >= 0; j--) {
                var r = logModel.get(j)
                if (r.rowType === "entry" && !seen[r.cid]) logModel.remove(j)
            }
            root.inscribedCount = count
        }, function () {})
    }

    // Re-arm pendingFinalizations from the persisted log (C++ index = entryIndex
    // for confirmInscription; cid keys the feed row). One function for both the
    // startup and the post-auth path — they used to be two diverging copies, one
    // of which dropped cid/libAtSubmit and broke the confirm message.
    function restoreInFlight(label) {
        if (!root.beacon) return
        logos.watch(root.beacon.getInscriptionLog(), function (rLogRaw) {
            var rLog = callModuleParse(rLogRaw)
            if (!Array.isArray(rLog)) return

            // Collect candidates first (index = C++ log index = entryIndex), then
            // resolve each source's channel via the async setupModuleChannel before
            // committing pendingFinalizations.
            var candidates = []
            for (var ri = 0; ri < rLog.length; ri++) {
                var re = rLog[ri]
                if ((re.status === "submitted" || re.status === "finalizing" || re.status === "queued")
                        && (re.slotFrom || 0) > 0) {
                    candidates.push({ index: ri, entry: re })
                }
            }
            if (candidates.length === 0) return

            var restored  = []
            var remaining = candidates.length
            function one() {
                remaining--
                if (remaining === 0 && restored.length > 0) {
                    root.pendingFinalizations = restored
                    appendActivity(label.replace("%1", restored.length), "info")
                }
            }
            for (var ci = 0; ci < candidates.length; ci++) {
                (function (c) {
                    var re = c.entry
                    function push(chId) {
                        restored.push({
                            entryIndex:  c.index,
                            channelId:   chId,
                            slotFrom:    re.slotFrom || 0,
                            libAtSubmit: re.libAtSubmit || 0,
                            cid:         re.cid || ""
                        })
                        one()
                    }
                    if (re.source && re.source.length > 0) {
                        setupModuleChannel(re.source, function (mc) {
                            push(mc ? mc.channelId : (root.channelId || ""))
                        })
                    } else {
                        push(root.channelId || "")
                    }
                })(candidates[ci])
            }
        }, function () {})
    }

    // ── Modules refresh ───────────────────────────────────────────────────────
    function refreshModules() {
        if (!root.beacon) return

        logos.watch(root.beacon.getModules(), function (raw) {
            var list = callModuleParse(raw)
            if (!Array.isArray(list)) return

            modulesModel.clear()
            for (var i = 0; i < list.length; i++) {
                modulesModel.append({
                    name:     list[i].name     || "",
                    cidCount: list[i].cidCount || 0,
                    lastTs:   list[i].lastTs   || 0
                })
                if (list[i].name) setupModuleChannel(list[i].name)   // async; caches
            }
        }, function () {})
    }

    // ── Startup ───────────────────────────────────────────────────────────────
    // The backend's QtRO context must be ready before any modules().logos_beacon
    // call succeeds, so gate the initial loads on isViewModuleReady / the
    // onViewModuleReadyChanged signal (the canonical universal-module readiness gate).
    function initFromBackend() {
        if (root._initialized) return
        if (!root.beacon) return
        root._initialized = true

        logos.watch(root.beacon.getBeaconConfig(), function (cfgRaw) {
            var cfg = callModuleParse(cfgRaw)
            if (!cfg) return
            root.nodeUrl         = cfg.nodeUrl         || "http://127.0.0.1:8080"
            root.explorerUrl     = cfg.explorerUrl     || root.explorerUrl
            root.persistencePath = cfg.persistencePath || ""
            root.channelLabel    = cfg.channelLabel    || "My Beacon"
            if (typeof nodeUrlInput !== "undefined")
                nodeUrlInput.text = root.nodeUrl
        }, function () {})

        logos.watch(root.beacon.getWatchedSources(), function (wsRawStr) {
            var wsRaw = callModuleParse(wsRawStr)
            if (wsRaw && Array.isArray(wsRaw.sources) && wsRaw.sources.length > 0)
                root.watchedSources = wsRaw.sources
            if (typeof sourcesInput !== "undefined")
                sourcesInput.text = root.watchedSources.join("\n")
        }, function () {})

        // Load already-manifested modules so we don't re-inscribe on restart
        logos.watch(root.beacon.getManifestLog(), function (mRaw) {
            var mList = callModuleParse(mRaw)
            if (Array.isArray(mList)) {
                var mm = {}
                for (var mi = 0; mi < mList.length; mi++) mm[mList[mi]] = true
                root.manifestedModules = mm
            }
        }, function () {})

        refreshLog()

        logos.watch(root.beacon.getNodeInfo(), function (niRaw) {
            var ni = callModuleParse(niRaw)
            if (ni && ni.lib_slot) root.currentLibSlot = ni.lib_slot
        }, function () {})

        // Restore in-flight finalizations that survived a restart
        restoreInFlight("restored %1 pending finalization(s) after restart")
    }

    Connections {
        target: logos
        function onViewModuleReadyChanged(moduleName, isReady) {
            if (moduleName === "beacon_ui") {
                root.beaconReady = isReady && root.beacon !== null
                if (root.beaconReady) root.initFromBackend()
            }
        }
    }

    Component.onCompleted: {
        if (typeof logos === "undefined" || !logos.module) return
        if (root.beacon !== null && logos.isViewModuleReady("beacon_ui")) {
            root.beaconReady = true
            root.initFromBackend()
        }
    }

    // ── Timers ────────────────────────────────────────────────────────────────
    Timer {
        id: stashPollTimer
        interval: 10000
        running:  true   // always running; pollModules() checks keycardConnected internally
        repeat:   true
        onTriggered: root.pollModules()
    }

    Timer {
        interval: 5000
        running:  root.keycardConnected
        repeat:   true
        onTriggered: {
            if (!root.beacon) return
            root.refreshLog()
            root.refreshModules()
            // Keep currentLibSlot fresh for in-flight progress bars
            logos.watch(root.beacon.getNodeInfo(), function (niRaw) {
                var ni = root.callModuleParse(niRaw)
                if (ni && ni.lib_slot) root.currentLibSlot = ni.lib_slot
            }, function () {})
        }
    }

    // ── #44 inclusion watchdog: config + re-inscribe helper ─────────────────────
    property int inclusionWindow:     120   // node slots (~2 min); not `safe` past this ⇒ dropped
    property int maxInclusionRetries: 4
    function republishItem(item) {
        if (!root.beacon) return
        // restored (pre-#44) items lack the key/payload → can't re-inscribe; skip quietly
        if (!item.channelId || !item.useKey || !item.payload) return
        // Empty checkpoint path ⇒ bootstrap re-fetches the current /channel tip, so the
        // retry chains correctly (root for a fresh channel, real tip otherwise).
        logos.watch(root.beacon.seqPublish(root.nodeUrl, item.channelId, item.useKey, "", item.payload),
            function (pubRaw) {
                if (root.seqIsError(pubRaw))
                    appendActivity("re-inscribe error: " + root.seqError(pubRaw), "error")
            },
            function (e) {})
    }

    // ── Finalization + inclusion watchdog — /channel/{id}: pending→safe→finalized (#43/#44) ──
    Timer {
        id: finalizationTimer
        interval: 6000
        running:  true
        repeat:   true
        onTriggered: {
            if (root.pendingFinalizations.length === 0) return
            if (root.finalizationBusy) return
            if (!root.beacon) return
            root.finalizationBusy = true

            logos.watch(root.beacon.getNodeInfo(), function (niRaw2) {
                var ni2 = root.callModuleParse(niRaw2)
                if (ni2 && ni2.lib_slot) root.currentLibSlot = ni2.lib_slot
                var libNow  = root.currentLibSlot
                var slotNow = (ni2 && ni2.slot) ? ni2.slot : 0

                var lookups   = root.pendingFinalizations.slice()   // check every pending item
                var remaining = []
                if (lookups.length === 0) {
                    root.pendingFinalizations = remaining
                    root.finalizationBusy = false
                    return
                }

                var pending = lookups.length
                function lookupDone() {
                    pending--
                    if (pending === 0) {
                        root.pendingFinalizations = remaining
                        root.finalizationBusy = false
                    }
                }

                for (var k = 0; k < lookups.length; k++) {
                    (function (item) {
                        var fRow = root.feedRowFor(item.cid || "")
                        // pending → safe → finalized via /channel/{id} (#43), with an
                        // inclusion watchdog that re-inscribes if the op never goes safe (#44).
                        logos.watch(root.beacon.getChannelState(item.channelId),
                            function (fRaw) {
                                var cs   = root.callModuleParse(fRaw)
                                var safe = cs && cs.found === true && cs.tipSlot >= 0

                                if (safe && cs.tipSlot <= libNow) {
                                    // FINALIZED (safe + slot ≤ LIB)
                                    var msgId = cs.tipMessage || ""
                                    logos.watch(root.beacon.confirmInscription(item.entryIndex, msgId, "confirmed"), function () {}, function () {})
                                    if (fRow >= 0) {
                                        if (msgId.length > 0) logModel.setProperty(fRow, "inscriptionId", msgId)
                                        logModel.setProperty(fRow, "status", "confirmed")
                                    }
                                    root.inscribedCount++
                                    appendActivity("confirmed: " + item.cid.substring(0, 16) + "…  slot " + cs.tipSlot, "success")
                                    // drop
                                } else if (safe) {
                                    // SAFE (in a block) but not yet finalized — awaiting finality
                                    if (fRow >= 0) {
                                        var s1 = logModel.get(fRow).status
                                        if (s1 !== "finalizing") {
                                            logModel.setProperty(fRow, "status", "finalizing")
                                            logos.watch(root.beacon.confirmInscription(item.entryIndex, "", "finalizing"), function () {}, function () {})
                                        }
                                    }
                                    remaining.push(item)
                                } else {
                                    // NOT safe (pending / dropped) — #44 inclusion watchdog
                                    var age = (slotNow > 0 && item.slotFrom > 0) ? (slotNow - item.slotFrom) : 0
                                    if (age <= root.inclusionWindow) {
                                        // still within the inclusion window — awaiting inclusion
                                        if (fRow >= 0) {
                                            var s2 = logModel.get(fRow).status
                                            if (s2 === "submitted" || s2 === "queued")
                                                logModel.setProperty(fRow, "status", "finalizing")
                                        }
                                        remaining.push(item)
                                    } else if ((item.retries || 0) < root.maxInclusionRetries) {
                                        // never went safe within the window → dropped from mempool.
                                        // re-inscribe (bootstrap re-fetches the current tip).
                                        item.retries  = (item.retries || 0) + 1
                                        item.slotFrom = slotNow            // fresh window for this attempt
                                        appendActivity("⚠ not included after ~" + root.inclusionWindow +
                                            " slots — re-inscribing " + item.cid.substring(0, 16) + "… (retry " +
                                            item.retries + "/" + root.maxInclusionRetries + ")", "info")
                                        root.republishItem(item)
                                        remaining.push(item)
                                    } else {
                                        // gave up after N retries
                                        logos.watch(root.beacon.confirmInscription(item.entryIndex, "", "failed"), function () {}, function () {})
                                        if (fRow >= 0) logModel.setProperty(fRow, "status", "failed")
                                        appendActivity("✗ failed — not included after " + root.maxInclusionRetries +
                                            " retries: " + item.cid.substring(0, 16) + "…", "error")
                                        // drop
                                    }
                                }
                                lookupDone()
                            },
                            function (e) { remaining.push(item); lookupDone() })
                    })(lookups[k])
                }
            }, function (e) { root.finalizationBusy = false })
        }
    }

    // ── Keycard auth poll ─────────────────────────────────────────────────────
    Timer {
        id: keycardAuthPollTimer
        interval: 750
        running: false
        repeat: true
        onTriggered: {
            if (root.keycardAuthId === "") {
                stop()
                root.authInFlight = false
                return
            }
            var raw = logos.callModule("keycard", "checkAuthStatus", [root.keycardAuthId])
            var r = root.callModuleParse(raw)
            if (!r) return
            if (r.status === "complete") {
                stop()
                root.authInFlight = false
                // setSigningKey is async now (backend → modules().logos_beacon).
                logos.watch(root.beacon.setSigningKey(r.key), function (skRaw) {
                    var skResult = root.callModuleParse(skRaw)
                    if (!skResult || skResult.error) {
                        root.keycardAuthStatus = "error"
                        return
                    }
                    root.signingKeyHex = r.key
                    // configureZoneSeq is async too; move completion into its callback.
                    root.configureZoneSeq(function (ok) {
                        if (ok) {
                            root.keycardAuthStatus = "complete"
                            root.keycardConnected  = true
                            appendActivity("Keycard authenticated", "success")
                            appendActivity("zone sequencer ready — " + root.channelId.substring(0,16) + "…", "success")
                            root.currentScreen     = "main"
                            root.refreshModules()

                            // Re-populate pendingFinalizations for any in-flight inscriptions
                            // (handles beacon restart while inscriptions were in submitted/finalizing state)
                            restoreInFlight("resumed %1 in-flight inscription(s)")
                        } else {
                            root.keycardAuthStatus = "error"
                        }
                    })
                }, function (e) { root.keycardAuthStatus = "error" })
            } else if (r.status === "rejected" || r.status === "failed") {
                stop()
                root.authInFlight      = false
                root.keycardAuthStatus = r.status
                root.keycardConnected  = false
            }
        }
    }

    // ── Card removal detection ────────────────────────────────────────────────
    Timer {
        id: cardCheckTimer
        interval: 8000
        running:  root.keycardConnected
        repeat:   true
        onTriggered: {
            if (typeof logos === "undefined" || !logos.callModule) return
            var raw = logos.callModule("keycard", "getState", [])
            var r = root.callModuleParse(raw)
            var cardGone = !r || r.state === "READER_NOT_FOUND" || r.state === "CARD_NOT_PRESENT"
            if (cardGone) {
                if (root.beacon)
                    logos.watch(root.beacon.clearSigningKey(), function () {}, function () {})
                root.keycardConnected  = false
                root.keycardAuthStatus = ""
                root.keycardAuthId     = ""
                root.signingKeyHex     = ""
                root.zoneSeqReady      = false
                root.channelId         = ""
                root.moduleChannels    = ({})
                root.currentScreen     = "landing"
                modulesModel.clear()
            }
        }
    }

    Component.onDestruction: {
        keycardAuthPollTimer.stop()
        cardCheckTimer.stop()
        stashPollTimer.stop()
        finalizationTimer.stop()
    }

    // ── Models ────────────────────────────────────────────────────────────────
    // logModel is the unified feed: inscription entries AND activity lines, one
    // chronological window (rowType: "entry" | "activity"). Entry rows are keyed
    // by cid (pinCid guarantees one row per cid); every row carries every role —
    // ListModel locks roles on first append.
    ListModel { id: logModel }
    ListModel { id: modulesModel }

    // ── Landing screen ────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: Theme.palette.background
        visible: root.currentScreen === "landing"

        ColumnLayout {
            anchors.centerIn: parent
            width: 320
            spacing: Theme.spacing.large

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing.tiny

                LogosText {
                    text: "Beacon"
                    font.pixelSize: Theme.typography.panelTitleText
                    font.weight: Theme.typography.weightBold
                    color: Theme.palette.text
                    Layout.alignment: Qt.AlignHCenter
                }

                LogosText {
                    text: "Permanent on-chain index. Inscribes uploaded CIDs into module-dedicated channels, each derived from your Keycard."
                    font.pixelSize: Theme.typography.secondaryText
                    color: Theme.palette.textSecondary
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            LogosText {
                Layout.fillWidth: true
                text: root.keycardAuthStatus === "pending"
                      ? "Switch to Keycard module to approve..." : ""
                color: Theme.palette.warning
                font.pixelSize: Theme.typography.primaryText
                horizontalAlignment: Text.AlignHCenter
                visible: text.length > 0
            }

            LogosText {
                Layout.fillWidth: true
                text: root.keycardAuthStatus === "rejected" ? "Authorization rejected. Try again." :
                      root.keycardAuthStatus === "error"    ? "Keycard not available. Try again." : ""
                color: Theme.palette.error
                font.pixelSize: Theme.typography.primaryText
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: text.length > 0
            }

            LogosButton {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                implicitHeight: 44
                radius: Theme.spacing.radiusPill
                text: (root.authInFlight || root.keycardAuthStatus === "pending")
                      ? "Requesting…" : "Connect with Keycard"
                enabled: !root.authInFlight && root.keycardAuthStatus !== "pending"
                onClicked: {
                    root.authInFlight = true
                    Qt.callLater(root.requestKeycardAuth)
                }
            }
        }
    }

    // ── Main UI ───────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: Theme.palette.background
        visible: root.currentScreen === "main"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacing.large
            spacing: Theme.spacing.medium

            // ── Header ────────────────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing.small

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing.tiny

                    LogosText {
                        text: "Beacon"
                        font.pixelSize: Theme.typography.panelTitleText
                        font.weight: Theme.typography.weightBold
                        color: Theme.palette.text
                    }

                    LogosText {
                        text: "Permanent on-chain index. Inscribes uploaded CIDs into module-dedicated channels, each derived from your Keycard."
                        font.pixelSize: Theme.typography.secondaryText
                        color: Theme.palette.textSecondary
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }

                // Status pill
                LogosBadge {
                    text: root.inscribedCount + " inscribed"
                    radius: Theme.spacing.radiusPill
                    color: root.zoneSeqReady ? Theme.palette.success : Theme.palette.error
                }

                // Gear button — settings toggle
                LogosButton {
                    implicitWidth: 28; implicitHeight: 28
                    Layout.preferredWidth: 28; Layout.preferredHeight: 28
                    radius: Theme.spacing.radiusMedium
                    text: "⚙"
                    onClicked: root.settingsPanelOpen = !root.settingsPanelOpen
                }
            }

            // ── Settings panel (bare Rectangle, no Item wrapper) ──────────────
            Rectangle {
                Layout.fillWidth: true
                visible: root.settingsPanelOpen
                height: settingsCol.implicitHeight + 20
                color: Theme.palette.backgroundSecondary
                radius: Theme.spacing.radiusMedium
                border.color: Theme.palette.borderHairline
                border.width: 1

                ColumnLayout {
                    id: settingsCol
                    anchors { top: parent.top; left: parent.left; right: parent.right; margins: 10 }
                    spacing: Theme.spacing.medium

                    // Channel ID row
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing.tiny

                        LogosText { text: "Channel ID"; color: Theme.palette.textSecondary; font.pixelSize: Theme.typography.secondaryText }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacing.small

                            Rectangle {
                                Layout.fillWidth: true
                                height: 32; color: Theme.palette.background; radius: Theme.spacing.radiusSmall
                                border.color: Theme.palette.borderHairline; border.width: 1

                                LogosText {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left; anchors.leftMargin: 8
                                    anchors.right: parent.right; anchors.rightMargin: 8
                                    text: root.channelId.length > 0
                                           ? root.channelId.substring(0, 16) + "..."
                                           : "(not yet derived)"
                                    color: root.channelId.length > 0 ? Theme.palette.text : Theme.palette.textMuted
                                    font.pixelSize: Theme.typography.secondaryText; font.family: root.monoFont
                                    elide: Text.ElideRight
                                }
                            }

                            LogosButton {
                                implicitWidth: 56; implicitHeight: 32
                                Layout.preferredWidth: 56; Layout.preferredHeight: 32
                                radius: Theme.spacing.radiusSmall
                                visible: root.channelId.length > 0
                                text: "Copy"
                                onClicked: root.copyToClipboard(root.channelId)
                            }
                        }
                    }

                    // Watched Sources row
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing.tiny

                        LogosText { text: "Watched Sources"; color: Theme.palette.textSecondary; font.pixelSize: Theme.typography.secondaryText }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacing.small

                            Rectangle {
                                Layout.fillWidth: true
                                height: 72
                                color: Theme.palette.background
                                radius: Theme.spacing.radiusSmall
                                border.color: Theme.palette.borderHairline
                                border.width: 1
                                clip: true

                                ScrollView {
                                    anchors.fill: parent
                                    anchors.margins: 6

                                    LogosTextArea {
                                        id: sourcesInput
                                        color: Theme.palette.text
                                        font.pixelSize: Theme.typography.secondaryText
                                        font.family: root.monoFont
                                        wrapMode: TextArea.NoWrap
                                        placeholderText: "keeper"
                                        placeholderTextColor: Theme.palette.textMuted
                                        background: null
                                        text: root.watchedSources.join("\n")
                                    }
                                }
                            }

                            LogosButton {
                                implicitWidth: 56; implicitHeight: 32
                                Layout.preferredWidth: 56; Layout.preferredHeight: 32
                                Layout.alignment: Qt.AlignTop
                                radius: Theme.spacing.radiusSmall
                                text: "Save"
                                onClicked: {
                                    if (!root.beacon) return
                                    logos.watch(root.beacon.setWatchedSources(sourcesInput.text), function () {}, function () {})
                                    root.watchedSources = sourcesInput.text.split("\n").filter(function(s){ return s.trim().length > 0 })
                                }
                            }
                        }
                    }

                    // Node URL row
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing.tiny

                        LogosText { text: "Node URL"; color: Theme.palette.textSecondary; font.pixelSize: Theme.typography.secondaryText }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacing.small

                            LogosTextField {
                                id: nodeUrlInput
                                Layout.fillWidth: true
                                implicitHeight: 32
                                placeholderText: "http://127.0.0.1:8080"
                                placeholderTextColor: Theme.palette.textMuted
                                text: root.nodeUrl
                            }

                            LogosButton {
                                implicitWidth: 56; implicitHeight: 32
                                Layout.preferredWidth: 56; Layout.preferredHeight: 32
                                radius: Theme.spacing.radiusSmall
                                text: "Save"
                                onClicked: {
                                    if (!root.beacon) return
                                    logos.watch(root.beacon.setNodeUrl(nodeUrlInput.text), function () {}, function () {})
                                    root.nodeUrl = nodeUrlInput.text
                                    configureZoneSeq()
                                }
                            }
                        }
                    }
                }
            }

            // ── Error banners ─────────────────────────────────────────────────
            Rectangle {
                visible: root.keycardAuthStatus !== "complete"
                Layout.fillWidth: true
                height: 30
                radius: Theme.spacing.radiusSmall
                color: root.keycardAuthStatus === "rejected" || root.keycardAuthStatus === "error"
                       ? Theme.colors.getColor(Theme.palette.error, 0.15)
                       : Theme.colors.getColor(Theme.palette.info, 0.12)

                LogosText {
                    anchors.centerIn: parent
                    text: root.keycardAuthStatus === ""         ? "Requesting Keycard auth..." :
                          root.keycardAuthStatus === "pending"  ? "Waiting for Keycard approval — open Keycard tab" :
                          root.keycardAuthStatus === "rejected" ? "Keycard auth rejected — reload to retry" :
                          root.keycardAuthStatus === "error"    ? "Keycard not available — key not loaded" : ""
                    color: root.keycardAuthStatus === "rejected" || root.keycardAuthStatus === "error"
                           ? Theme.palette.error : Theme.palette.textSecondary
                    font.pixelSize: Theme.typography.secondaryText
                }
            }

            Rectangle {
                visible: root.keycardConnected && !root.zoneSeqReady
                Layout.fillWidth: true
                height: 30
                radius: Theme.spacing.radiusSmall
                color: Theme.colors.getColor(Theme.palette.error, 0.15)

                LogosText {
                    anchors.centerIn: parent
                    text: "Zone sequencer unavailable — logos_beacon could not derive a channel"
                    color: Theme.palette.error
                    font.pixelSize: Theme.typography.secondaryText
                }
            }

            // ── Main content: Channels (25%) + Log (75%) ──────────────────────
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // ── Channels column ───────────────────────────────────────────
                ColumnLayout {
                    id: channelsCol
                    anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
                    width: Math.floor(parent.width * 0.25) - 6
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            spacing: 1

                            LogosText {
                                text: "Channels"
                                font.pixelSize: Theme.typography.primaryText
                                font.weight: Theme.typography.weightBold
                                color: Theme.palette.text
                            }

                            LogosText {
                                text: "auto-detected"
                                font.pixelSize: Theme.typography.badgeText
                                color: Theme.palette.textMuted
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Theme.palette.backgroundSecondary
                        radius: Theme.spacing.radiusMedium
                        border.color: Theme.palette.borderHairline
                        border.width: 1
                        clip: true

                        LogosText {
                            anchors.centerIn: parent
                            visible: modulesModel.count === 0
                            text: "No channels yet"
                            color: Theme.palette.textMuted
                            font.pixelSize: Theme.typography.secondaryText
                        }

                        ListView {
                            id: channelsListView
                            anchors { fill: parent; margins: 8 }
                            model: modulesModel
                            clip: true
                            spacing: Theme.spacing.tiny

                            delegate: Rectangle {
                                width: channelsListView.width
                                height: modCol.implicitHeight + 10
                                color: modRowArea.containsMouse ? Theme.palette.backgroundElevated : "transparent"
                                radius: Theme.spacing.radiusSmall
                                Behavior on color { ColorAnimation { duration: 80 } }

                                ColumnLayout {
                                    id: modCol
                                    anchors.left: parent.left; anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 6; anchors.rightMargin: 6
                                    spacing: 2

                                    LogosText {
                                        text: model.name || "(primary)"
                                        color: Theme.palette.text
                                        font.pixelSize: Theme.typography.secondaryText
                                        font.weight: Theme.typography.weightBold
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    LogosText {
                                        text: {
                                            var mc = root.moduleChannels[model.name]
                                            if (mc && mc.channelId && mc.channelId.length > 0)
                                                return mc.channelId.substring(0, 12) + "..."
                                            return model.name ? "deriving..." : ""
                                        }
                                        color: Theme.palette.textMuted
                                        font.pixelSize: Theme.typography.badgeText
                                        font.family: root.monoFont
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    LogosText {
                                        text: model.cidCount + " CIDs"
                                        color: Theme.palette.textSecondary
                                        font.pixelSize: Theme.typography.badgeText
                                    }
                                }

                                MouseArea {
                                    id: modRowArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                }
                            }
                        }
                    }
                }

                // ── Log column ────────────────────────────────────────────────
                ColumnLayout {
                    anchors {
                        top: parent.top; bottom: parent.bottom
                        left: channelsCol.right; leftMargin: 12
                        right: parent.right
                    }
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        spacing: Theme.spacing.tiny

                        LogosText {
                            text: "Log & Activity"
                            font.pixelSize: Theme.typography.primaryText
                            font.weight: Theme.typography.weightBold
                            color: Theme.palette.text
                            Layout.fillWidth: true
                        }

                        // Copy-all log button — visible text label. (icons/Copy.svg
                        // isn't bundled into the .lgx → the old Image button rendered
                        // blank/invisible; #33. A text label needs no external asset.)
                        LogosText {
                            id: copyAllBtn
                            text: "Copy"
                            font.pixelSize: Theme.typography.secondaryText
                            color: copyAllArea.containsMouse ? Theme.palette.textSecondary : Theme.palette.textMuted
                            Behavior on color { ColorAnimation { duration: 120 } }
                            Timer { id: copyResetTimer; interval: 1200; onTriggered: copyAllBtn.text = "Copy" }

                            MouseArea {
                                id: copyAllArea
                                anchors.fill: parent
                                anchors.margins: -6
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    var lines = []
                                    for (var i = 0; i < logModel.count; i++) {
                                        var r = logModel.get(i)
                                        if (r.rowType === "activity") {
                                            lines.push("[" + r.tsStr + "] " + r.msg)
                                            continue
                                        }
                                        var src = r.source || "primary"
                                        var fname = r.label
                                        if (r.label.indexOf("Logos Storage: ") === 0) {
                                            var parts = r.label.split(" → ")
                                            fname = parts[0].replace("Logos Storage: ", "")
                                        }
                                        var chain = (src && src !== "primary")
                                            ? "[" + src + " → stash → Logos Storage]"
                                            : "[primary]"
                                        lines.push("[" + r.tsStr + "] Inscribed " + r.cid.substring(0, 12) + "… for " + fname + " " + chain)
                                    }
                                    root.copyToClipboard(lines.join("\n"))
                                    copyAllBtn.text = "Copied"
                                    copyResetTimer.restart()
                                }
                            }
                        }

                        LogosText {
                            text: "Clear"
                            font.pixelSize: Theme.typography.secondaryText
                            color: clearLogArea.containsMouse ? Theme.palette.textSecondary : Theme.palette.textMuted
                            Behavior on color { ColorAnimation { duration: 120 } }
                            MouseArea {
                                id: clearLogArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (root.beacon)
                                        logos.watch(root.beacon.clearInscriptionLog(), function () {}, function () {})
                                    logModel.clear()
                                }
                            }
                        }
                    }

                    // ── Free-text inscribe (#25) ──────────────────────────────
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing.small

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: Math.min(72, Math.max(32, inscribeInput.implicitHeight + 12))
                            color: Theme.palette.backgroundSecondary
                            radius: Theme.spacing.radiusSmall
                            border.color: inscribeInput.activeFocus ? Theme.palette.primary : Theme.palette.borderHairline
                            border.width: 1
                            clip: true

                            ScrollView {
                                anchors.fill: parent
                                anchors.margins: 6

                                LogosTextArea {
                                    id: inscribeInput
                                    color: Theme.palette.text
                                    font.pixelSize: Theme.typography.secondaryText
                                    font.family: root.monoFont
                                    wrapMode: TextArea.Wrap
                                    placeholderText: "Inscribe anything — e.g. {\"v\":1,\"type\":\"ia_item\",\"id\":\"…\",\"name\":\"…\",\"size\":123}"
                                    placeholderTextColor: Theme.palette.textMuted
                                    background: null
                                }
                            }
                        }

                        LogosButton {
                            id: inscribeBtn
                            implicitWidth: 72; implicitHeight: 32
                            Layout.preferredWidth: 72; Layout.preferredHeight: 32
                            Layout.alignment: Qt.AlignTop
                            radius: Theme.spacing.radiusSmall
                            text: root.inscribeBusy ? "…" : "Inscribe"
                            // primary CTA: filled brand fill instead of LogosButton's subtle default
                            background: Rectangle {
                                radius: inscribeBtn.radius
                                color: !inscribeBtn.enabled ? Theme.palette.backgroundMuted
                                     : inscribeBtn.isActive ? Theme.palette.primaryPressed
                                     : Theme.palette.primary
                            }
                            // pollBusy deliberately NOT in this binding — the 5/6/10s
                            // poll timers flip it constantly and the button would
                            // flicker; inscribeCid() rejects busy clicks itself
                            enabled: root.zoneSeqReady
                                     && inscribeInput.text.trim().length > 0
                                     && !root.inscribeBusy
                            onClicked: {
                                var t = inscribeInput.text.trim()
                                if (t.length === 0) return
                                inscribeInput.text = ""        // request registered — input is free
                                root.inscribeBusy = true
                                root.appendActivity("inscribe requested — publishing "
                                    + t.substring(0, 40) + (t.length > 40 ? "…" : ""), "info")
                                root._pendingInscribeText = t
                                inscribeKick.start()
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Theme.palette.backgroundSecondary
                        radius: Theme.spacing.radiusMedium
                        border.color: Theme.palette.borderHairline
                        border.width: 1
                        clip: true

                        LogosText {
                            anchors.centerIn: parent
                            visible: logModel.count === 0
                            text: "Nothing yet — inscriptions and activity land here"
                            color: Theme.palette.textMuted
                            font.pixelSize: Theme.typography.secondaryText
                            font.family: root.monoFont
                        }

                        ListView {
                            id: logListView
                            anchors { fill: parent; margins: 10 }
                            model: logModel
                            clip: true
                            spacing: Theme.spacing.tiny
                            onCountChanged: Qt.callLater(() => logListView.positionViewAtEnd())

                            delegate: Rectangle {
                                id: logEntry
                                required property int    index
                                required property string rowType
                                required property string cid
                                required property string label
                                required property string source
                                required property string tsStr
                                required property string inscriptionId
                                required property string status
                                required property int    slotFrom
                                required property int    libAtSubmit
                                required property string msg
                                required property string level

                                width: logListView.width
                                implicitHeight: rowType === "activity"
                                                ? actLine.implicitHeight + 8
                                                : entryCol.implicitHeight + 12
                                color: "transparent"
                                radius: Theme.spacing.radiusSmall
                                border.width: 1
                                border.color: Theme.palette.borderHairline

                                // 3px status/level accent stripe
                                Rectangle {
                                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                                    width: 3
                                    radius: Theme.spacing.radiusSmall
                                    color: logEntry.rowType === "activity"
                                        ? (logEntry.level === "success" ? Theme.palette.success
                                         : logEntry.level === "error"   ? Theme.palette.error
                                         : logEntry.level === "muted"   ? Theme.palette.textSecondary
                                         : Theme.palette.info)
                                        : (logEntry.isDone   ? Theme.palette.success
                                         : logEntry.isFailed ? Theme.palette.error
                                         : logEntry.status === "finalizing" ? Theme.palette.primary
                                         : Theme.palette.warning)
                                }

                                // Activity row — one selectable monospace line
                                TextEdit {
                                    id: actLine
                                    visible: logEntry.rowType === "activity"
                                    anchors { left: parent.left; right: parent.right; top: parent.top
                                              leftMargin: Theme.spacing.small; rightMargin: Theme.spacing.small
                                              topMargin: Theme.spacing.tiny }
                                    text: "[" + logEntry.tsStr + "] " + logEntry.msg
                                    color: logEntry.level === "success" ? Theme.palette.success
                                         : logEntry.level === "error"   ? Theme.palette.error
                                         : logEntry.level === "muted"   ? Theme.palette.textMuted
                                         : Theme.palette.textSecondary
                                    font.pixelSize: Theme.typography.secondaryText; font.family: root.monoFont
                                    wrapMode: Text.WrapAnywhere; readOnly: true; selectByMouse: true
                                    selectedTextColor: Theme.palette.background; selectionColor: Theme.palette.textSecondary
                                }

                                // Progress: lib advancing from libAtSubmit toward slotFrom
                                property real progressVal: {
                                    if (status === "confirmed" || status === "ok") return 1.0
                                    if (status === "failed" || status === "error") return 0.0
                                    if (slotFrom <= 0 || libAtSubmit <= 0) return 0.0
                                    var total = slotFrom - libAtSubmit
                                    if (total <= 0) return 0.0
                                    var elapsed = root.currentLibSlot - libAtSubmit
                                    // Hold below 100% until the explorer scan actually confirms the
                                    // tx — lib-catchup finishing only means "finalizable", not
                                    // "finalized". The bar reaching 1.0 is reserved for confirmed.
                                    return Math.min(0.95, Math.max(0.0, elapsed / total))
                                }

                                // Remaining slots → ~M:SS estimate
                                property string timeEst: {
                                    if (status === "confirmed" || status === "ok"
                                            || status === "failed" || status === "error") return ""
                                    if (slotFrom <= 0 || root.currentLibSlot <= 0) return ""
                                    var rem = slotFrom - root.currentLibSlot
                                    if (rem <= 0) return ""
                                    var mins = Math.floor(rem / 60)
                                    var secs = rem % 60
                                    return "~" + mins + ":" + (secs < 10 ? "0" : "") + secs
                                }

                                property string explorerUrl:
                                    inscriptionId.length > 0
                                    ? root.explorerUrl + "/txs/" + inscriptionId
                                    : ""

                                property bool inFlight: status === "queued"
                                                     || status === "submitted"
                                                     || status === "finalizing"
                                property bool isDone:   status === "confirmed" || status === "ok"
                                property bool isFailed: status === "failed" || status === "error"
                                // lib has caught up to the submit slot but the explorer scan hasn't
                                // returned the tx hash yet → finalization imminent. Show "confirming…"
                                // instead of a stuck full bar with no link.
                                property bool confirming: inFlight && slotFrom > 0
                                                       && root.currentLibSlot >= slotFrom

                                ColumnLayout {
                                    id: entryCol
                                    visible: logEntry.rowType === "entry"
                                    anchors { left: parent.left; right: parent.right
                                              top: parent.top; topMargin: Theme.spacing.tiny
                                              leftMargin: Theme.spacing.small; rightMargin: Theme.spacing.small }
                                    spacing: Theme.spacing.tiny

                                    // Line 1: [time] label   source   status badge
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.spacing.small

                                        LogosText {
                                            text: "[" + tsStr + "] " + (label.length > 0 ? label : cid.substring(0, 20) + "…")
                                            font.pixelSize: Theme.typography.secondaryText
                                            font.family: root.monoFont
                                            color: logEntry.isDone   ? Theme.palette.success
                                                 : logEntry.isFailed ? Theme.palette.error
                                                 : Theme.palette.text
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }

                                        LogosText {
                                            visible: source.length > 0
                                            text: source
                                            font.pixelSize: Theme.typography.badgeText
                                            color: Theme.palette.textMuted
                                        }

                                        // Status pill (submitted/queued/finalizing/confirmed/failed)
                                        LogosBadge {
                                            visible: status.length > 0
                                            text: status
                                            radius: Theme.spacing.radiusPill
                                            color: status === "confirmed" || status === "ok" ? Theme.palette.success
                                                 : status === "failed" || status === "error" ? Theme.palette.error
                                                 : status === "finalizing" ? Theme.palette.primary
                                                 : Theme.palette.warning
                                        }
                                    }

                                    // Line 2: CID  +  progress bar / hash + copy URL / failed
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.spacing.small

                                        LogosText {
                                            // pseudo-cids ("text:<epoch>") key free-text rows — noise, not content
                                            visible: cid.indexOf("text:") !== 0
                                            text: cid.length > 0 ? cid.substring(0, 20) + "…" : "…"
                                            font.pixelSize: Theme.typography.secondaryText
                                            font.family: root.monoFont
                                            color: Theme.palette.textMuted
                                        }

                                        // Progress bar (in-flight only)
                                        Rectangle {
                                            visible: logEntry.inFlight
                                            Layout.fillWidth: true
                                            height: 4; radius: Theme.spacing.radiusSmall
                                            color: Theme.palette.backgroundSecondary

                                            Rectangle {
                                                width: parent.width * logEntry.progressVal
                                                height: parent.height; radius: parent.radius
                                                color: Theme.palette.primary
                                                Behavior on width { NumberAnimation { duration: 600 } }
                                            }
                                        }

                                        // Time estimate while lib catches up; then "confirming…"
                                        // until the explorer scan returns the tx hash (status → confirmed).
                                        LogosText {
                                            visible: logEntry.inFlight
                                                     && (logEntry.timeEst.length > 0 || logEntry.confirming)
                                            text: logEntry.confirming ? "confirming…" : logEntry.timeEst
                                            font.pixelSize: Theme.typography.secondaryText
                                            color: logEntry.confirming ? Theme.palette.primary : Theme.palette.textMuted
                                        }

                                        // Truncated hash (confirmed)
                                        LogosText {
                                            visible: logEntry.isDone && inscriptionId.length > 0
                                            text: inscriptionId.substring(0, 16) + "…"
                                            font.pixelSize: Theme.typography.secondaryText
                                            font.family: root.monoFont
                                            color: Theme.palette.success
                                        }

                                        // Copy URL button (confirmed)
                                        LogosButton {
                                            visible: logEntry.isDone && logEntry.explorerUrl.length > 0
                                            implicitHeight: 18
                                            implicitWidth: 72
                                            Layout.preferredHeight: 18
                                            radius: Theme.spacing.radiusSmall
                                            text: "copy URL"
                                            onClicked: root.copyToClipboard(logEntry.explorerUrl)
                                        }

                                        // Failed label
                                        LogosText {
                                            visible: logEntry.isFailed
                                            text: "failed"
                                            font.pixelSize: Theme.typography.secondaryText
                                            color: Theme.palette.error
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── Footer ────────────────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true

                LogosText {
                    text: "Learn more about Beacon"
                    font.pixelSize: Theme.typography.secondaryText
                    color: Theme.palette.textSecondary
                }

                Item {
                    width: 20; height: 20

                    Image {
                        anchors.centerIn: parent
                        width: 14; height: 14
                        source: "icons/Copy.svg"
                        fillMode: Image.PreserveAspectFit
                        opacity: footerCopyArea.pressed ? 0.6
                               : footerCopyArea.containsMouse ? 1.0 : 0.4
                        Behavior on opacity { NumberAnimation { duration: 120 } }
                    }

                    MouseArea {
                        id: footerCopyArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.copyToClipboard("https://github.com/xAlisher/beacon-basecamp")
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }
    }
}
