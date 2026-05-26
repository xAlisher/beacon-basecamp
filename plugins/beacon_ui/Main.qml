import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: root

    // ── Palette (Stash-aligned) ────────────────────────────────────────────────
    readonly property color bgPrimary:     "#171717"
    readonly property color bgSecondary:   "#262626"
    readonly property color bgActive:      "#332A27"
    readonly property color textPrimary:   "#FFFFFF"
    readonly property color textSecondary: "#A4A4A4"
    readonly property color textMuted:     "#5D5D5D"
    readonly property color accentOrange:  "#FF5000"
    readonly property color accentHover:   "#FF6B1A"
    readonly property color accentPressed: "#CC4000"
    readonly property color successGreen:  "#22C55E"
    readonly property color errorRed:      "#FB3748"
    readonly property color warningYellow: "#FFC107"
    readonly property color borderColor:   "#383838"

    // ── State ─────────────────────────────────────────────────────────────────
    property string channelId:         ""
    property string nodeUrl:           "http://127.0.0.1:8080"
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

    // ── Screen state ──────────────────────────────────────────────────────────
    property string currentScreen: "landing"   // "landing" | "main"

    // ── Keycard auth state ────────────────────────────────────────────────────
    property string keycardAuthId:     ""
    property string keycardAuthStatus: ""
    property bool   keycardConnected:  false
    property bool   authInFlight:      false   // guard against concurrent auth calls

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

    // ── Activity log helper ───────────────────────────────────────────────────
    function appendActivity(msg, level) {
        var ts = "[" + Qt.formatDateTime(new Date(), "HH:mm:ss") + "]"
        if (activityLogModel.count >= 200) activityLogModel.remove(0)
        activityLogModel.append({ ts: ts, msg: msg, level: level || "info" })
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
        logos.callModule("logos_beacon", "clearSigningKey", [])
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
    function configureZoneSeq() {
        if (typeof logos === "undefined" || !logos.callModule) return
        if (root.signingKeyHex === "") return

        logos.callModule("logos_beacon", "ensureCheckpointsDir", [])

        logos.callModule("liblogos_zone_sequencer_module", "set_signing_key",
                         [root.signingKeyHex])
        logos.callModule("liblogos_zone_sequencer_module", "set_node_url",
                         [root.nodeUrl])
        logos.callModule("liblogos_zone_sequencer_module", "set_checkpoint_path",
                         [root.persistencePath + "/beacon.checkpoint"])

        var chRaw = logos.callModule("liblogos_zone_sequencer_module",
                                     "get_channel_id", [])
        var ch = callModuleParse(chRaw)
        var derivedId = ""
        if (typeof ch === 'string' && ch.length > 0 &&
                !ch.toLowerCase().startsWith("error")) {
            derivedId = ch
        } else if (ch && ch.channelId) {
            derivedId = ch.channelId
        }

        if (derivedId.length > 0) {
            logos.callModule("liblogos_zone_sequencer_module",
                             "set_channel_id", [derivedId])
            root.channelId    = derivedId
            root.zoneSeqReady = true
        }
    }

    // ── Channel manifest inscription ─────────────────────────────────────────
    // Inscribes a channel_manifest entry to the primary Beacon channel so Cord
    // can discover all module channels from a single Beacon key lookup.
    function inscribeManifest(name, channelId) {
        if (!root.zoneSeqReady || root.signingKeyHex === "" || root.channelId === "") return

        // Fully re-initialize primary channel (mirrors configureZoneSeq) so any
        // temporary signing-key switch in setupModuleChannel doesn't leave stale state
        logos.callModule("liblogos_zone_sequencer_module", "set_signing_key", [root.signingKeyHex])
        logos.callModule("liblogos_zone_sequencer_module", "set_checkpoint_path",
                         [root.persistencePath + "/beacon.checkpoint"])
        logos.callModule("liblogos_zone_sequencer_module", "set_channel_id", [""])
        var chRaw2 = logos.callModule("liblogos_zone_sequencer_module", "get_channel_id", [])
        var ch2 = callModuleParse(chRaw2)
        var derivedId = typeof ch2 === 'string' ? ch2 : (ch2 && ch2.channelId ? ch2.channelId : "")
        if (!derivedId || derivedId.length === 0 || derivedId.toLowerCase().startsWith("error")) {
            appendActivity("manifest error for " + name + " (re-derive failed)", "error")
            return
        }
        logos.callModule("liblogos_zone_sequencer_module", "set_channel_id", [derivedId])

        var payload = JSON.stringify({
            v:          1,
            type:       "channel_manifest",
            module:     name,
            channel_id: channelId,
            ts:         Math.floor(Date.now() / 1000)
        })

        var pubRaw    = logos.callModule("liblogos_zone_sequencer_module", "publish", [payload])
        var pubResult = callModuleParse(pubRaw)

        var isError = false
        if (typeof pubResult === 'string')
            isError = pubResult.toLowerCase().startsWith("error") || pubResult.length === 0
        else if (pubResult && pubResult.error)
            isError = true

        if (!isError) {
            logos.callModule("logos_beacon", "recordManifest", [name])
            var updated = {}
            for (var k in root.manifestedModules) updated[k] = root.manifestedModules[k]
            updated[name] = true
            root.manifestedModules = updated
            appendActivity("manifested " + name + " channel — " + channelId.substring(0,16) + "…", "success")
        } else {
            appendActivity("manifest error for " + name, "error")
        }
    }

    // ── Per-module channel derivation ─────────────────────────────────────────
    function setupModuleChannel(name) {
        if (root.moduleChannels[name]) return

        var raw = logos.callModule("logos_beacon", "deriveModuleSigningKey", [name])
        var r = callModuleParse(raw)
        if (!r || !r.signingKey) return
        var sk = r.signingKey

        logos.callModule("liblogos_zone_sequencer_module", "set_signing_key", [sk])
        logos.callModule("liblogos_zone_sequencer_module", "set_channel_id", [""])
        var chRaw = logos.callModule("liblogos_zone_sequencer_module", "get_channel_id", [])
        var ch = callModuleParse(chRaw)
        var channelId = typeof ch === "string" ? ch : (ch && ch.channelId ? ch.channelId : "")

        // Restore primary channel before any early return
        logos.callModule("liblogos_zone_sequencer_module",
                         "set_signing_key", [root.signingKeyHex])
        logos.callModule("liblogos_zone_sequencer_module",
                         "set_channel_id", [root.channelId])

        if (!channelId || channelId.toLowerCase().startsWith("error")) return

        var updated = {}
        var existing = root.moduleChannels
        for (var key in existing) updated[key] = existing[key]
        updated[name] = { signingKey: sk, channelId: channelId }
        root.moduleChannels = updated
    }

    // ── Inscription flow ──────────────────────────────────────────────────────
    function inscribeCid(cid, label, source) {
        if (root.pollBusy) return
        root.pollBusy = true

        var useKey, useChannelId, useCheckpoint
        if (source && source.length > 0) {
            setupModuleChannel(source)
            var mc = root.moduleChannels[source]
            if (mc) {
                useKey        = mc.signingKey
                useChannelId  = mc.channelId
                useCheckpoint = root.persistencePath + "/checkpoints/" + source + ".checkpoint"
            } else {
                appendActivity("using primary channel for " + source, "info")
                useKey        = root.signingKeyHex
                useChannelId  = root.channelId
                useCheckpoint = root.persistencePath + "/beacon.checkpoint"
            }
        } else {
            useKey        = root.signingKeyHex
            useChannelId  = root.channelId
            useCheckpoint = root.persistencePath + "/beacon.checkpoint"
        }

        var pinRaw = logos.callModule("logos_beacon", "pinCid", [cid, label, source || ""])
        var pin    = callModuleParse(pinRaw)

        if (!pin || pin.error) {
            appendActivity("error: pinCid " + (pin ? pin.error : "null"), "error")
            root.pollBusy = false; return false
        }
        if (pin.duplicate === true) {
            appendActivity("duplicate: " + cid.substring(0,16) + "…", "muted")
            root.pollBusy = false; return true  // confirmed on-chain — caller should markInscribed
        }

        var entryIndex = pin.entryIndex

        var now = new Date()
        logModel.insert(entryIndex, {
            cid:           cid,
            label:         label,
            source:        source || "",
            tsStr:         Qt.formatDateTime(now, "HH:mm:ss"),
            inscriptionId: "",
            status:        "pending"
        })

        var payload = JSON.stringify({
            v:      1,
            type:   "cid_pin",
            cid:    cid,
            label:  label,
            source: source || "",
            ts:     Math.floor(Date.now() / 1000)
        })

        var pubRaw
        if (useChannelId === root.channelId) {
            // Primary beacon channel — use the persistent sequencer handle (fast path)
            pubRaw = logos.callModule("liblogos_zone_sequencer_module", "publish", [payload])
        } else {
            // Module sub-channel — use stateless publish_to so the correct channel is used.
            // publish() would silently go to the primary handle regardless of set_channel_id.
            pubRaw = logos.callModule("liblogos_zone_sequencer_module",
                                      "publish_to", [useChannelId, useKey, useCheckpoint, payload])
        }
        var pubResult = callModuleParse(pubRaw)

        var inscriptionId = ""
        var status        = "error"

        var isError = false
        if (typeof pubResult === 'string') {
            isError = pubResult.toLowerCase().startsWith("error") || pubResult.length === 0
        } else if (pubResult && pubResult.error) {
            isError = true
        }

        if (!isError) {
            if (typeof pubResult === 'string') {
                inscriptionId = pubResult
                status        = "ok"
            } else if (pubResult && pubResult.inscriptionId) {
                inscriptionId = pubResult.inscriptionId
                status        = "ok"
            } else if (pubResult && pubResult.id) {
                inscriptionId = pubResult.id
                status        = "ok"
            }
        }

        logos.callModule("logos_beacon", "confirmInscription",
                         [entryIndex, inscriptionId, status])

        if (entryIndex >= 0 && entryIndex < logModel.count) {
            logModel.setProperty(entryIndex, "inscriptionId", inscriptionId)
            logModel.setProperty(entryIndex, "status", status)
            if (status === "ok") root.inscribedCount++
        }

        if (status === "ok")
            appendActivity("[" + (source || "primary") + "] inscribed " + cid.substring(0,16) + "…", "success")
        else
            appendActivity("[" + (source || "primary") + "] inscription error — " + cid.substring(0,16) + "…", "error")

        // Manifest module channel to primary Beacon channel on first successful inscription
        if (status === "ok" && source && source.length > 0
                && root.moduleChannels[source]
                && !root.manifestedModules[source]) {
            inscribeManifest(source, root.moduleChannels[source].channelId)
        }

        root.pollBusy = false
        return status === "ok"
    }

    // ── Broadcast channel announce (kept for future use; not exposed in UI) ───
    function broadcastChannel() {
        if (root.pollBusy) return
        if (!root.zoneSeqReady || root.channelId === "") return
        root.pollBusy = true
        root.broadcastStatus = ""

        logos.callModule("logos_beacon", "setChannelLabel", [root.channelLabel])

        var payload = JSON.stringify({
            v:          1,
            type:       "channel_announce",
            module:     "logos_beacon",
            channel_id: root.channelId,
            label:      root.channelLabel,
            ts:         Math.floor(Date.now() / 1000)
        })

        var pubRaw    = logos.callModule("liblogos_zone_sequencer_module",
                                         "publish", [payload])
        var pubResult = callModuleParse(pubRaw)

        var isError = false
        if (typeof pubResult === 'string') {
            isError = pubResult.toLowerCase().startsWith("error") || pubResult.length === 0
        } else if (pubResult && pubResult.error) {
            isError = true
        }

        root.broadcastStatus = isError ? "error" : "ok"
        root.pollBusy = false
    }

    // ── Module inscription queue polling ─────────────────────────────────────
    function pollModules() {
        if (root.pollBusy) return
        if (!root.keycardConnected) return
        if (typeof logos === "undefined" || !logos.callModule) return
        if (root.watchedSources.length === 0) return

        root.pollBusy = true

        for (var i = 0; i < root.watchedSources.length; i++) {
            var moduleName = root.watchedSources[i]
            var queueRaw   = logos.callModule(moduleName, "getInscriptionQueue", [])
            var queue      = callModuleParse(queueRaw)
            if (!Array.isArray(queue) || queue.length === 0) continue

            for (var j = 0; j < queue.length; j++) {
                var entry = queue[j]
                if (!entry.cid) continue
                appendActivity("queued from " + moduleName + ": " + entry.cid.substring(0, 16) + "…", "info")
                root.pollBusy = false
                var inscribed = inscribeCid(entry.cid, entry.label || entry.cid, moduleName)
                root.pollBusy = true
                if (inscribed)
                    logos.callModule(moduleName, "markInscribed", [entry.cid])
            }
        }

        root.pollBusy = false
    }

    // ── Log refresh ───────────────────────────────────────────────────────────
    function refreshLog() {
        if (typeof logos === "undefined" || !logos.callModule) return

        var raw     = logos.callModule("logos_beacon", "getInscriptionLog", [])
        var entries = callModuleParse(raw)
        if (!Array.isArray(entries)) return

        logModel.clear()
        var count = 0
        for (var i = 0; i < entries.length; i++) {
            var e  = entries[i]
            var ts = new Date(e.ts * 1000)
            logModel.append({
                cid:           e.cid    || "",
                label:         e.label  || "",
                source:        e.source || "",
                tsStr:         Qt.formatDateTime(ts, "HH:mm:ss"),
                inscriptionId: e.inscriptionId || "",
                status:        e.status || "pending"
            })
            if (e.status === "ok") count++
        }
        root.inscribedCount = count
    }

    // ── Modules refresh ───────────────────────────────────────────────────────
    function refreshModules() {
        if (typeof logos === "undefined" || !logos.callModule) return

        var raw  = logos.callModule("logos_beacon", "getModules", [])
        var list = callModuleParse(raw)
        if (!Array.isArray(list)) return

        modulesModel.clear()
        for (var i = 0; i < list.length; i++) {
            modulesModel.append({
                name:     list[i].name     || "",
                cidCount: list[i].cidCount || 0,
                lastTs:   list[i].lastTs   || 0
            })
            if (list[i].name) setupModuleChannel(list[i].name)
        }
    }

    // ── Startup ───────────────────────────────────────────────────────────────
    Component.onCompleted: {
        if (typeof logos === "undefined" || !logos.callModule) return

        var cfgRaw = logos.callModule("logos_beacon", "getBeaconConfig", [])
        var cfg    = callModuleParse(cfgRaw)
        if (!cfg) return

        root.nodeUrl         = cfg.nodeUrl         || "http://127.0.0.1:8080"

        var wsRaw = callModuleParse(logos.callModule("logos_beacon", "getWatchedSources", []))
        if (wsRaw && Array.isArray(wsRaw.sources) && wsRaw.sources.length > 0)
            root.watchedSources = wsRaw.sources
        if (typeof sourcesInput !== "undefined")
            sourcesInput.text = root.watchedSources.join("\n")

        root.persistencePath = cfg.persistencePath || ""
        root.channelLabel    = cfg.channelLabel    || "My Beacon"

        // Load already-manifested modules so we don't re-inscribe on restart
        var mRaw = logos.callModule("logos_beacon", "getManifestLog", [])
        var mList = callModuleParse(mRaw)
        if (Array.isArray(mList)) {
            var mm = {}
            for (var mi = 0; mi < mList.length; mi++) mm[mList[mi]] = true
            root.manifestedModules = mm
        }

        nodeUrlInput.text = root.nodeUrl

        refreshLog()
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
            root.refreshLog()
            root.refreshModules()
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
                var skRaw = logos.callModule("logos_beacon", "setSigningKey", [r.key])
                var skResult = root.callModuleParse(skRaw)
                if (!skResult || skResult.error) {
                    root.keycardAuthStatus = "error"
                    return
                }
                root.signingKeyHex = r.key
                root.configureZoneSeq()
                if (root.zoneSeqReady) {
                    root.keycardAuthStatus = "complete"
                    root.keycardConnected  = true
                    appendActivity("Keycard authenticated", "success")
                    appendActivity("zone sequencer ready — " + root.channelId.substring(0,16) + "…", "success")
                    root.currentScreen     = "main"
                    root.refreshModules()
                } else {
                    root.keycardAuthStatus = "error"
                }
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
                logos.callModule("logos_beacon", "clearSigningKey", [])
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
    }

    // ── Models ────────────────────────────────────────────────────────────────
    ListModel { id: logModel }
    ListModel { id: modulesModel }
    ListModel { id: activityLogModel }

    // ── Landing screen ────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: root.bgPrimary
        visible: root.currentScreen === "landing"

        ColumnLayout {
            anchors.centerIn: parent
            width: 320
            spacing: 16

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: "Beacon"
                    font.pixelSize: 20
                    font.bold: true
                    color: root.textPrimary
                    Layout.alignment: Qt.AlignHCenter
                }

                Text {
                    text: "Permanent on-chain index. Inscribes uploaded CIDs into module-dedicated channels, each derived from your Keycard."
                    font.pixelSize: 11
                    color: root.textSecondary
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Text {
                Layout.fillWidth: true
                text: root.keycardAuthStatus === "pending"
                      ? "Switch to Keycard module to approve..." : ""
                color: root.warningYellow
                font.pixelSize: 13
                horizontalAlignment: Text.AlignHCenter
                visible: text.length > 0
            }

            Text {
                Layout.fillWidth: true
                text: root.keycardAuthStatus === "rejected" ? "Authorization rejected. Try again." :
                      root.keycardAuthStatus === "error"    ? "Keycard not available. Try again." : ""
                color: root.errorRed
                font.pixelSize: 13
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: text.length > 0
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                radius: 22
                color: (root.authInFlight || root.keycardAuthStatus === "pending")
                       ? root.warningYellow
                       : (connectArea.containsMouse ? root.accentHover : root.accentOrange)

                Text {
                    anchors.centerIn: parent
                    text: (root.authInFlight || root.keycardAuthStatus === "pending")
                          ? "Requesting…" : "Connect with Keycard"
                    color: root.textPrimary
                    font.pixelSize: 14
                    font.weight: Font.Medium
                }

                MouseArea {
                    id: connectArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    enabled: !root.authInFlight && root.keycardAuthStatus !== "pending"
                    onClicked: {
                        root.authInFlight = true
                        Qt.callLater(root.requestKeycardAuth)
                    }
                }
            }
        }
    }

    // ── Main UI ───────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: root.bgPrimary
        visible: root.currentScreen === "main"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            // ── Header ────────────────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: "Beacon"
                        font.pixelSize: 20
                        font.bold: true
                        color: root.textPrimary
                    }

                    Text {
                        text: "Permanent on-chain index. Inscribes uploaded CIDs into module-dedicated channels, each derived from your Keycard."
                        font.pixelSize: 11
                        color: root.textSecondary
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }

                // Status pill
                Rectangle {
                    height: 28
                    implicitWidth: statusPillRow.implicitWidth + 20
                    radius: 14
                    color: Qt.rgba(0.149, 0.149, 0.149, 0.85)
                    border.color: root.borderColor
                    border.width: 1

                    RowLayout {
                        id: statusPillRow
                        anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                        spacing: 6

                        Rectangle {
                            width: 7; height: 7; radius: 4
                            Layout.alignment: Qt.AlignVCenter
                            color: root.zoneSeqReady ? root.successGreen : root.errorRed
                        }

                        Text {
                            text: root.inscribedCount + " inscribed"
                            font.pixelSize: 11
                            color: root.textPrimary
                        }
                    }
                }

                // Gear button — settings toggle
                Rectangle {
                    width: 28; height: 28
                    radius: 6
                    color: gearArea.containsMouse ? root.bgSecondary : "transparent"
                    border.color: root.settingsPanelOpen ? root.accentOrange : root.borderColor
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "⚙"
                        font.pixelSize: 14
                        color: root.settingsPanelOpen ? root.accentOrange : root.textSecondary
                    }

                    MouseArea {
                        id: gearArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.settingsPanelOpen = !root.settingsPanelOpen
                    }
                }
            }

            // ── Settings panel (bare Rectangle, no Item wrapper) ──────────────
            Rectangle {
                Layout.fillWidth: true
                visible: root.settingsPanelOpen
                height: settingsCol.implicitHeight + 20
                color: root.bgSecondary
                radius: 6
                border.color: root.borderColor
                border.width: 1

                ColumnLayout {
                    id: settingsCol
                    anchors { top: parent.top; left: parent.left; right: parent.right; margins: 10 }
                    spacing: 12

                    // Channel ID row
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text { text: "Channel ID"; color: root.textSecondary; font.pixelSize: 11 }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Rectangle {
                                Layout.fillWidth: true
                                height: 32; color: root.bgPrimary; radius: 4
                                border.color: root.borderColor; border.width: 1

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left; anchors.leftMargin: 8
                                    anchors.right: parent.right; anchors.rightMargin: 8
                                    text: root.channelId.length > 0
                                           ? root.channelId.substring(0, 16) + "..."
                                           : "(not yet derived)"
                                    color: root.channelId.length > 0 ? root.textPrimary : root.textMuted
                                    font.pixelSize: 12; font.family: "monospace"
                                    elide: Text.ElideRight
                                }
                            }

                            Rectangle {
                                width: 56; height: 32; radius: 4
                                visible: root.channelId.length > 0
                                color: chCopyArea.pressed      ? root.accentPressed
                                     : chCopyArea.containsMouse ? root.accentHover : root.accentOrange
                                Behavior on color { ColorAnimation { duration: 100 } }

                                Text { anchors.centerIn: parent; text: "Copy"; color: "#FFFFFF"; font.pixelSize: 12; font.bold: true }

                                MouseArea {
                                    id: chCopyArea
                                    anchors.fill: parent; hoverEnabled: true
                                    onClicked: root.copyToClipboard(root.channelId)
                                }
                            }
                        }
                    }

                    // Watched Sources row
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text { text: "Watched Sources"; color: root.textSecondary; font.pixelSize: 11 }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Rectangle {
                                Layout.fillWidth: true
                                height: 72
                                color: root.bgPrimary
                                radius: 4
                                border.color: root.borderColor
                                border.width: 1
                                clip: true

                                ScrollView {
                                    anchors.fill: parent
                                    anchors.margins: 6

                                    TextArea {
                                        id: sourcesInput
                                        color: root.textPrimary
                                        font.pixelSize: 12
                                        font.family: "monospace"
                                        wrapMode: TextArea.NoWrap
                                        placeholderText: "keeper"
                                        placeholderTextColor: root.textMuted
                                        background: null
                                        text: root.watchedSources.join("\n")
                                    }
                                }
                            }

                            Rectangle {
                                width: 56; height: 32; radius: 4
                                Layout.alignment: Qt.AlignTop
                                color: srcSaveArea.pressed      ? root.accentPressed
                                     : srcSaveArea.containsMouse ? root.accentHover : root.accentOrange
                                Behavior on color { ColorAnimation { duration: 100 } }

                                Text { anchors.centerIn: parent; text: "Save"; color: "#FFFFFF"; font.pixelSize: 12; font.bold: true }

                                MouseArea {
                                    id: srcSaveArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (typeof logos === "undefined" || !logos.callModule) return
                                        logos.callModule("logos_beacon", "setWatchedSources", [sourcesInput.text])
                                        root.watchedSources = sourcesInput.text.split("\n").filter(function(s){ return s.trim().length > 0 })
                                    }
                                }
                            }
                        }
                    }

                    // Node URL row
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text { text: "Node URL"; color: root.textSecondary; font.pixelSize: 11 }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Rectangle {
                                Layout.fillWidth: true
                                height: 32; color: root.bgPrimary; radius: 4
                                border.color: nodeUrlInput.activeFocus ? root.accentOrange : root.borderColor
                                border.width: 1
                                Behavior on border.color { ColorAnimation { duration: 100 } }

                                TextField {
                                    id: nodeUrlInput
                                    anchors.fill: parent; anchors.margins: 1
                                    color: root.textPrimary; font.pixelSize: 12; font.family: "monospace"
                                    background: null; leftPadding: 8
                                    placeholderText: "http://127.0.0.1:8080"
                                    placeholderTextColor: root.textMuted
                                    text: root.nodeUrl
                                }
                            }

                            Rectangle {
                                width: 56; height: 32; radius: 4
                                color: saveArea.pressed      ? root.accentPressed
                                     : saveArea.containsMouse ? root.accentHover : root.accentOrange
                                Behavior on color { ColorAnimation { duration: 100 } }

                                Text { anchors.centerIn: parent; text: "Save"; color: "#FFFFFF"; font.pixelSize: 12; font.bold: true }

                                MouseArea {
                                    id: saveArea
                                    anchors.fill: parent; hoverEnabled: true
                                    onClicked: {
                                        if (typeof logos === "undefined") return
                                        logos.callModule("logos_beacon", "setNodeUrl", [nodeUrlInput.text])
                                        root.nodeUrl = nodeUrlInput.text
                                        configureZoneSeq()
                                    }
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
                radius: 4
                color: root.keycardAuthStatus === "rejected" || root.keycardAuthStatus === "error"
                       ? "#2A1515" : "#1A1A2A"

                Text {
                    anchors.centerIn: parent
                    text: root.keycardAuthStatus === ""         ? "Requesting Keycard auth..." :
                          root.keycardAuthStatus === "pending"  ? "Waiting for Keycard approval — open Keycard tab" :
                          root.keycardAuthStatus === "rejected" ? "Keycard auth rejected — reload to retry" :
                          root.keycardAuthStatus === "error"    ? "Keycard not available — key not loaded" : ""
                    color: root.keycardAuthStatus === "rejected" || root.keycardAuthStatus === "error"
                           ? root.errorRed : root.textSecondary
                    font.pixelSize: 11
                }
            }

            Rectangle {
                visible: root.keycardConnected && !root.zoneSeqReady
                Layout.fillWidth: true
                height: 30
                radius: 4
                color: "#2A1515"

                Text {
                    anchors.centerIn: parent
                    text: "Zone sequencer unavailable — install liblogos_zone_sequencer_module"
                    color: root.errorRed
                    font.pixelSize: 11
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
                        width: parent.width

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Text {
                                text: "Channels"
                                font.pixelSize: 13
                                font.bold: true
                                color: root.textPrimary
                            }

                            Text {
                                text: "auto-detected"
                                font.pixelSize: 9
                                color: root.textMuted
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: root.bgSecondary
                        radius: 6
                        border.color: root.borderColor
                        border.width: 1
                        clip: true

                        Text {
                            anchors.centerIn: parent
                            visible: modulesModel.count === 0
                            text: "No channels yet"
                            color: root.textMuted
                            font.pixelSize: 11
                        }

                        ListView {
                            id: channelsListView
                            anchors { fill: parent; margins: 8 }
                            model: modulesModel
                            clip: true
                            spacing: 2

                            delegate: Rectangle {
                                width: channelsListView.width
                                height: modCol.implicitHeight + 10
                                color: modRowArea.containsMouse ? Qt.rgba(0.22, 0.22, 0.22, 1) : "transparent"
                                radius: 3
                                Behavior on color { ColorAnimation { duration: 80 } }

                                ColumnLayout {
                                    id: modCol
                                    anchors.left: parent.left; anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 6; anchors.rightMargin: 6
                                    spacing: 2

                                    Text {
                                        text: model.name || "(primary)"
                                        color: root.textPrimary
                                        font.pixelSize: 11
                                        font.bold: true
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: {
                                            var mc = root.moduleChannels[model.name]
                                            if (mc && mc.channelId && mc.channelId.length > 0)
                                                return mc.channelId.substring(0, 12) + "..."
                                            return model.name ? "deriving..." : ""
                                        }
                                        color: root.textMuted
                                        font.pixelSize: 9
                                        font.family: "monospace"
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: model.cidCount + " CIDs"
                                        color: root.textSecondary
                                        font.pixelSize: 9
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
                        spacing: 4

                        Text {
                            text: "Log"
                            font.pixelSize: 13
                            font.bold: true
                            color: root.textPrimary
                            Layout.fillWidth: true
                        }

                        // Copy-all button
                        Item {
                            width: 20; height: 20

                            Image {
                                anchors.centerIn: parent
                                width: 16; height: 16
                                source: "icons/Copy.svg"
                                fillMode: Image.PreserveAspectFit
                                opacity: copyAllArea.pressed ? 0.6
                                       : copyAllArea.containsMouse ? 1.0 : 0.4
                                Behavior on opacity { NumberAnimation { duration: 120 } }
                            }

                            MouseArea {
                                id: copyAllArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    var lines = []
                                    for (var i = 0; i < logModel.count; i++) {
                                        var r = logModel.get(i)
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
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "#0D0D0D"
                        radius: 6
                        border.color: root.borderColor
                        border.width: 1
                        clip: true

                        Text {
                            anchors.centerIn: parent
                            visible: logModel.count === 0
                            text: "No inscriptions yet"
                            color: root.textMuted
                            font.pixelSize: 11
                            font.family: "Courier New, monospace"
                        }

                        ListView {
                            id: logListView
                            anchors { fill: parent; margins: 10 }
                            model: logModel
                            clip: true
                            spacing: 2
                            onCountChanged: Qt.callLater(() => logListView.positionViewAtEnd())

                            delegate: TextEdit {
                                required property string cid
                                required property string label
                                required property string source
                                required property string tsStr
                                required property string inscriptionId
                                required property string status

                                width: logListView.width
                                text: {
                                    var fname = label
                                    if (label.indexOf("Logos Storage: ") === 0) {
                                        var parts = label.split(" → ")
                                        fname = parts[0].replace("Logos Storage: ", "")
                                    }
                                    var chain = (source && source !== "primary")
                                        ? "[" + source + " → stash → Logos Storage]"
                                        : "[primary]"
                                    return "[" + tsStr + "] Inscribed " + (cid.length > 0 ? cid.substring(0, 12) : "unknown") + "… for " + fname + " " + chain
                                }
                                color: status === "ok"    ? root.successGreen
                                     : status === "error" ? root.errorRed
                                     : root.warningYellow
                                font.pixelSize: 11
                                font.family: "Courier New, monospace"
                                wrapMode: Text.WrapAnywhere
                                readOnly: true
                                selectByMouse: true
                                selectedTextColor: root.bgPrimary
                                selectionColor: root.textSecondary
                            }
                        }
                    }
                }
            }

            // ── Activity log (full width, below channels+inscription) ────────────
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: "Activity"
                        font.pixelSize: 11; font.bold: true
                        color: root.textSecondary
                        Layout.fillWidth: true
                    }

                    Item {
                        width: 20; height: 20
                        Image {
                            anchors.centerIn: parent; width: 16; height: 16
                            source: "icons/Copy.svg"; fillMode: Image.PreserveAspectFit
                            opacity: actCopyArea.pressed ? 0.6 : actCopyArea.containsMouse ? 1.0 : 0.4
                            Behavior on opacity { NumberAnimation { duration: 120 } }
                        }
                        MouseArea {
                            id: actCopyArea; anchors.fill: parent
                            hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                var lines = []
                                for (var i = 0; i < activityLogModel.count; i++) {
                                    var e = activityLogModel.get(i)
                                    lines.push(e.ts + " " + e.msg)
                                }
                                root.copyToClipboard(lines.join("\n"))
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 110
                    color: "#0D0D0D"
                    radius: 4
                    border.color: root.borderColor; border.width: 1
                    clip: true

                    Text {
                        anchors.centerIn: parent
                        visible: activityLogModel.count === 0
                        text: "No activity yet"
                        color: root.textMuted; font.pixelSize: 11
                        font.family: "Courier New, monospace"
                    }

                    ListView {
                        id: actListView
                        anchors { fill: parent; margins: 8 }
                        model: activityLogModel; clip: true; spacing: 1
                        onCountChanged: Qt.callLater(() => actListView.positionViewAtEnd())

                        delegate: TextEdit {
                            required property string ts
                            required property string msg
                            required property string level
                            width: actListView.width
                            text: ts + " " + msg
                            color: level === "success" ? root.successGreen
                                 : level === "error"   ? root.errorRed
                                 : level === "muted"   ? root.textMuted
                                 : root.textSecondary
                            font.pixelSize: 11; font.family: "Courier New, monospace"
                            wrapMode: Text.WrapAnywhere; readOnly: true; selectByMouse: true
                            selectedTextColor: root.bgPrimary; selectionColor: root.textSecondary
                        }
                    }
                }
            }

            // ── Footer ────────────────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "Learn more about Beacon"
                    font.pixelSize: 11
                    color: root.textSecondary
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
