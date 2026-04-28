import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: root

    // ── Palette (matches stash/keycard/notes design language) ─────────────────
    readonly property color bgPrimary:     "#171717"
    readonly property color bgSecondary:   "#262626"
    readonly property color textPrimary:   "#FFFFFF"
    readonly property color textSecondary: "#A4A4A4"
    readonly property color textMuted:     "#666666"
    readonly property color accent:        "#FF5000"
    readonly property color accentHover:   "#FF6B1A"
    readonly property color accentPressed: "#CC4000"
    readonly property color successGreen:  "#4CAF50"
    readonly property color errorRed:      "#F44336"
    readonly property color warningAmber:  "#FFC107"
    readonly property color borderColor:   "#333333"

    // ── State ─────────────────────────────────────────────────────────────────
    property string channelId:       ""
    property string nodeUrl:         "http://127.0.0.1:8080"
    property string signingKeyHex:   ""
    property string persistencePath: ""
    property bool   watchStash:      true
    property bool   zoneSeqReady:    false
    property bool   settingsPanelOpen: false

    property int  stashSeenCount: 0
    property bool pollBusy:       false
    property int  inscribedCount: 0
    property string channelLabel:    "My Beacon"
    property string broadcastStatus: ""   // "" | "ok" | "error"

    // ── Screen state ──────────────────────────────────────────────────────────
    property string currentScreen: "landing"   // "landing" | "main"

    // ── Keycard auth state ────────────────────────────────────────────────────
    property string keycardAuthId:      ""
    property string keycardAuthStatus:  ""
    property bool   keycardConnected:   false

    // ── Per-module channels cache ─────────────────────────────────────────────
    // {moduleName: {signingKey, channelId}}
    property var moduleChannels: ({})

    // ── Hidden clipboard helper ───────────────────────────────────────────────
    TextEdit {
        id: clipboardHelper
        visible: false
    }

    function copyToClipboard(text) {
        clipboardHelper.text = text
        clipboardHelper.selectAll()
        clipboardHelper.copy()
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

    // ── Keycard auth request ──────────────────────────────────────────────────
    function requestKeycardAuth() {
        if (typeof logos === "undefined" || !logos.callModule) return
        if (root.keycardAuthStatus === "pending" && root.keycardAuthId !== "") return
        logos.callModule("logos_beacon", "clearSigningKey", [])
        root.keycardConnected  = false
        root.keycardAuthStatus = ""
        var raw = logos.callModule("keycard", "requestAuth", ["bc:beacon", "logos_beacon"])
        var r = callModuleParse(raw)
        if (r && r.authId) {
            root.keycardAuthId     = r.authId
            root.keycardAuthStatus = "pending"
            keycardAuthPollTimer.start()
        } else {
            root.keycardAuthStatus = "error"
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

    // ── Per-module channel derivation ─────────────────────────────────────────
    // Derives and caches the signing key + channel ID for a named module.
    // Uses the set_channel_id("") trick to clear zone seq's cache before re-deriving.
    // Always restores the primary channel config before returning.
    function setupModuleChannel(name) {
        if (root.moduleChannels[name]) return   // already cached

        var raw = logos.callModule("logos_beacon", "deriveModuleSigningKey", [name])
        var r = callModuleParse(raw)
        if (!r || !r.signingKey) return
        var sk = r.signingKey

        // Derive channel ID for this module key
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

        // Resolve channel for this source
        var useKey, useChannelId, useCheckpoint
        if (source && source.length > 0) {
            setupModuleChannel(source)
            var mc = root.moduleChannels[source]
            if (!mc) { root.pollBusy = false; return }
            useKey        = mc.signingKey
            useChannelId  = mc.channelId
            useCheckpoint = root.persistencePath + "/checkpoints/" + source + ".checkpoint"
        } else {
            useKey        = root.signingKeyHex
            useChannelId  = root.channelId
            useCheckpoint = root.persistencePath + "/beacon.checkpoint"
        }

        var pinRaw = logos.callModule("logos_beacon", "pinCid", [cid, label, source || ""])
        var pin    = callModuleParse(pinRaw)

        if (!pin || pin.error) { root.pollBusy = false; return }
        if (pin.duplicate === true) { root.pollBusy = false; return }

        var entryIndex = pin.entryIndex

        // Show pending row immediately in the UI
        var now = new Date()
        logModel.insert(entryIndex, {
            cid:           cid,
            label:         label,
            source:        source || "",
            tsStr:         Qt.formatDateTime(now, "HH:mm:ss"),
            inscriptionId: "",
            status:        "pending"
        })

        // Configure zone seq for the module channel
        logos.callModule("liblogos_zone_sequencer_module", "set_signing_key", [useKey])
        logos.callModule("liblogos_zone_sequencer_module", "set_checkpoint_path", [useCheckpoint])
        logos.callModule("liblogos_zone_sequencer_module", "set_channel_id", [useChannelId])

        var payload = JSON.stringify({
            v:      1,
            type:   "cid_pin",
            cid:    cid,
            label:  label,
            source: source || "",
            ts:     Math.floor(Date.now() / 1000)
        })

        var pubRaw    = logos.callModule("liblogos_zone_sequencer_module",
                                         "publish", [payload])
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

        // Restore primary channel config
        logos.callModule("liblogos_zone_sequencer_module",
                         "set_signing_key", [root.signingKeyHex])
        logos.callModule("liblogos_zone_sequencer_module",
                         "set_checkpoint_path",
                         [root.persistencePath + "/beacon.checkpoint"])
        logos.callModule("liblogos_zone_sequencer_module",
                         "set_channel_id", [root.channelId])

        root.pollBusy = false
    }

    // ── Broadcast channel announce ────────────────────────────────────────────
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

    // ── Stash log polling ─────────────────────────────────────────────────────
    function extractCid(text) {
        var m = text.match(/\b(Qm[1-9A-HJ-NP-Za-km-z]{44}|baf[a-zA-Z0-9]{50,})\b/)
        return m ? m[1] : ""
    }

    function pollStash() {
        if (root.pollBusy) return
        if (!root.watchStash) return
        if (!root.keycardConnected) return
        if (typeof logos === "undefined" || !logos.callModule) return

        root.pollBusy = true

        var raw     = logos.callModule("stash", "getLog", [])
        var entries = callModuleParse(raw)

        if (!Array.isArray(entries)) { root.pollBusy = false; return }

        for (var i = root.stashSeenCount; i < entries.length; i++) {
            var e   = entries[i]
            var cid = ""
            if (e.cid && e.cid.length > 0) {
                cid = e.cid
            } else if (e.detail) {
                cid = extractCid(e.detail)
            }
            if (cid !== "") {
                var lbl = e.detail ? e.detail : ("stash upload " + cid.substring(0, 12))
                root.pollBusy = false
                inscribeCid(cid, lbl, "stash")
                root.pollBusy = true
            }
        }

        root.stashSeenCount = entries.length
        root.pollBusy       = false
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
        root.watchStash      = cfg.watchStash !== false
        root.persistencePath = cfg.persistencePath || ""
        root.channelLabel    = cfg.channelLabel    || "My Beacon"

        nodeUrlInput.text      = root.nodeUrl
        channelLabelInput.text = root.channelLabel

        refreshLog()
    }

    // ── Timers ────────────────────────────────────────────────────────────────
    Timer {
        id: stashPollTimer
        interval: 10000
        running:  root.watchStash && root.keycardConnected
        repeat:   true
        onTriggered: root.pollStash()
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
            if (root.keycardAuthId === "") { stop(); return }
            var raw = logos.callModule("keycard", "checkAuthStatus", [root.keycardAuthId])
            var r = root.callModuleParse(raw)
            if (!r) return
            if (r.status === "complete") {
                stop()
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
                    root.currentScreen     = "main"
                    root.refreshModules()
                } else {
                    root.keycardAuthStatus = "error"
                }
            } else if (r.status === "rejected" || r.status === "failed") {
                stop()
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

    // ── Landing screen ────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: root.bgPrimary
        visible: root.currentScreen === "landing"

        ColumnLayout {
            anchors.centerIn: parent
            width: 300
            spacing: 16

            Text {
                text: "Beacon"
                font.pixelSize: 28
                font.weight: Font.Bold
                color: root.textPrimary
                Layout.alignment: Qt.AlignHCenter
            }

            Text {
                text: "Inscribe CIDs on-chain with Keycard"
                color: root.textSecondary
                font.pixelSize: 14
                Layout.alignment: Qt.AlignHCenter
            }

            Text {
                Layout.fillWidth: true
                text: root.keycardAuthStatus === "pending"
                      ? "Switch to Keycard module to approve..." : ""
                color: root.warningAmber
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
                color: root.keycardAuthStatus === "pending"
                       ? root.warningAmber
                       : (connectArea.containsMouse ? root.accentHover : root.accent)

                Text {
                    anchors.centerIn: parent
                    text: root.keycardAuthStatus === "pending"
                          ? "Waiting for approval..." : "Connect with Keycard"
                    color: root.textPrimary
                    font.pixelSize: 14
                    font.weight: Font.Medium
                }

                MouseArea {
                    id: connectArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    enabled: root.keycardAuthStatus !== "pending"
                    onClicked: {
                        root.keycardAuthStatus = ""
                        root.requestKeycardAuth()
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
            spacing: 0

            // ── Header ────────────────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "Beacon"
                    color: root.textPrimary
                    font.pixelSize: 18
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                Rectangle {
                    width: 8; height: 8; radius: 4
                    color: root.zoneSeqReady ? root.successGreen : root.errorRed
                }

                Text {
                    text: root.inscribedCount + " inscribed"
                    color: root.textSecondary
                    font.pixelSize: 12
                }

                // Gear icon — toggles settings panel
                Rectangle {
                    width: 28; height: 28; radius: 4
                    color: gearArea.containsMouse ? root.bgSecondary : "transparent"
                    Behavior on color { ColorAnimation { duration: 100 } }

                    Text {
                        anchors.centerIn: parent
                        text: "⚙"
                        color: root.settingsPanelOpen ? root.accent : root.textSecondary
                        font.pixelSize: 16
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

            // ── Error banners ─────────────────────────────────────────────────
            Rectangle {
                visible: root.keycardAuthStatus !== "complete"
                Layout.fillWidth: true
                height: 30
                radius: 4
                color: root.keycardAuthStatus === "rejected" || root.keycardAuthStatus === "error"
                       ? "#2A1515" : "#1A1A2A"
                Layout.topMargin: 4

                Text {
                    anchors.centerIn: parent
                    text: root.keycardAuthStatus === ""        ? "Requesting Keycard auth..." :
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
                Layout.topMargin: 8

                Text {
                    anchors.centerIn: parent
                    text: "Zone sequencer unavailable — install liblogos_zone_sequencer_module"
                    color: root.errorRed
                    font.pixelSize: 11
                }
            }

            // ── Settings panel (collapsible, gear icon toggle) ────────────────
            Item {
                visible: root.settingsPanelOpen
                Layout.fillWidth: true
                implicitHeight: settingsCol.implicitHeight + 24
                Layout.topMargin: 8

                Rectangle {
                    anchors.fill: parent
                    color: root.bgSecondary
                    radius: 6
                    border.color: root.borderColor
                    border.width: 1
                }

                ColumnLayout {
                    id: settingsCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 12
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
                                color: chCopyArea.pressed     ? root.accentPressed
                                     : chCopyArea.containsMouse ? root.accentHover : root.accent
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

                    // Channel label + Broadcast row
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text { text: "Channel Label"; color: root.textSecondary; font.pixelSize: 11 }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Rectangle {
                                Layout.fillWidth: true
                                height: 32; color: root.bgPrimary; radius: 4
                                border.color: channelLabelInput.activeFocus ? root.accent : root.borderColor
                                border.width: 1
                                Behavior on border.color { ColorAnimation { duration: 100 } }

                                TextField {
                                    id: channelLabelInput
                                    anchors.fill: parent; anchors.margins: 1
                                    color: root.textPrimary; font.pixelSize: 12
                                    background: null; leftPadding: 8
                                    placeholderText: "My Beacon"; placeholderTextColor: root.textMuted
                                    text: root.channelLabel
                                    onTextChanged: root.channelLabel = text
                                }
                            }

                            Rectangle {
                                width: 100; height: 32; radius: 4
                                color: broadcastArea.pressed      ? root.accentPressed
                                     : broadcastArea.containsMouse ? root.accentHover
                                     : (root.zoneSeqReady && root.channelId !== "") ? root.accent : "#555555"
                                opacity: (root.zoneSeqReady && root.channelId !== "") ? 1.0 : 0.6
                                Behavior on color { ColorAnimation { duration: 100 } }
                                Text { anchors.centerIn: parent; text: "Broadcast"; color: "#FFFFFF"; font.pixelSize: 12; font.bold: true }
                                MouseArea {
                                    id: broadcastArea
                                    anchors.fill: parent; hoverEnabled: true
                                    enabled: root.zoneSeqReady && root.channelId !== ""
                                    onClicked: root.broadcastChannel()
                                }
                                ToolTip.visible: broadcastArea.containsMouse
                                ToolTip.text: root.zoneSeqReady && root.channelId !== ""
                                    ? "Inscribe channel_announce to your Beacon channel"
                                    : "Zone sequencer not ready"
                            }
                        }

                        Text {
                            visible: root.broadcastStatus !== ""
                            text: root.broadcastStatus === "ok"
                                ? "✓ Channel announced on-chain"
                                : "✗ Broadcast failed — check zone sequencer"
                            color: root.broadcastStatus === "ok" ? root.successGreen : root.errorRed
                            font.pixelSize: 11
                            Layout.fillWidth: true
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
                                border.color: nodeUrlInput.activeFocus ? root.accent : root.borderColor
                                border.width: 1
                                Behavior on border.color { ColorAnimation { duration: 100 } }

                                TextField {
                                    id: nodeUrlInput
                                    anchors.fill: parent; anchors.margins: 1
                                    color: root.textPrimary; font.pixelSize: 12; font.family: "monospace"
                                    background: null; leftPadding: 8
                                    placeholderText: "http://127.0.0.1:8080"; placeholderTextColor: root.textMuted
                                    text: root.nodeUrl
                                }
                            }

                            Rectangle {
                                width: 56; height: 32; radius: 4
                                color: saveArea.pressed      ? root.accentPressed
                                     : saveArea.containsMouse ? root.accentHover : root.accent
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

                    // Watch stash toggle
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text { text: "Auto-watch Stash"; color: root.textSecondary; font.pixelSize: 13; Layout.fillWidth: true }

                        Switch {
                            id: watchStashSwitch
                            checked: root.watchStash
                            onCheckedChanged: {
                                root.watchStash = checked
                                if (typeof logos !== "undefined")
                                    logos.callModule("logos_beacon", "setWatchStash", [checked])
                            }
                        }
                    }
                }
            }

            // ── Split pane: Modules (25%) + Log (75%) ─────────────────────────
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: 12

                // ── Modules panel ─────────────────────────────────────────────
                ColumnLayout {
                    id: modulesPanel
                    x: 0; y: 0
                    width: Math.floor(parent.width * 0.25)
                    height: parent.height
                    spacing: 0

                    Text {
                        text: "Modules"
                        color: root.textMuted
                        font.pixelSize: 10
                        Layout.fillWidth: true
                    }

                    Rectangle {
                        Layout.fillWidth: true; height: 1
                        color: root.borderColor
                        Layout.topMargin: 4
                        Layout.bottomMargin: 4
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: modulesModel
                        clip: true
                        spacing: 2

                        delegate: Rectangle {
                            width: parent ? parent.width : 0
                            height: modCol.implicitHeight + 10
                            color: modRowArea.containsMouse ? root.bgSecondary : "transparent"
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

                // ── Log panel ─────────────────────────────────────────────────
                ColumnLayout {
                    x: modulesPanel.width + 12
                    y: 0
                    width: parent.width - modulesPanel.width - 12
                    height: parent.height
                    spacing: 0

                    // Header: "Log" + copy-all
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            text: "Log"
                            color: root.textMuted
                            font.pixelSize: 10
                            Layout.fillWidth: true
                        }

                        Item {
                            width: 20; height: 20

                            Rectangle {
                                x: 0; y: 3; width: 12; height: 13
                                color: "transparent"
                                border.color: copyAllArea.containsMouse ? root.textPrimary : root.textMuted
                                border.width: 1; radius: 1
                                Behavior on border.color { ColorAnimation { duration: 100 } }
                            }
                            Rectangle {
                                x: 4; y: 0; width: 12; height: 13
                                color: root.bgPrimary
                                border.color: copyAllArea.containsMouse ? root.textPrimary : root.textMuted
                                border.width: 1; radius: 1
                                Behavior on border.color { ColorAnimation { duration: 100 } }
                            }

                            MouseArea {
                                id: copyAllArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    var lines = []
                                    for (var i = 0; i < logModel.count; i++) {
                                        var r = logModel.get(i)
                                        var src = r.source || "primary"
                                        var verb = r.status === "error" ? "was not inscribed" : "successfully inscribed"
                                        var statusWord = r.status === "ok" ? "Confirmed"
                                                        : r.status === "error" ? "Error" : "Pending"
                                        lines.push(r.tsStr + " [" + src + "] " + r.label +
                                                   " CID " + r.cid + " " + verb +
                                                   " on LEZ, status: " + statusWord)
                                    }
                                    root.copyToClipboard(lines.join("\n"))
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true; height: 1
                        color: root.borderColor
                        Layout.topMargin: 4
                        Layout.bottomMargin: 4
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: logModel
                        clip: true
                        spacing: 2
                        verticalLayoutDirection: ListView.BottomToTop  // newest at bottom

                        delegate: Rectangle {
                            width: parent ? parent.width : 0
                            height: msgText.implicitHeight + 10
                            color: rowArea.containsMouse ? root.bgSecondary : "transparent"
                            radius: 3
                            Behavior on color { ColorAnimation { duration: 80 } }

                            property color statusColor:
                                model.status === "ok"    ? root.successGreen :
                                model.status === "error" ? root.errorRed     : root.warningAmber

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 4
                                anchors.rightMargin: 4
                                anchors.topMargin: 5
                                anchors.bottomMargin: 5
                                spacing: 5

                                // Status dot
                                Rectangle {
                                    width: 7; height: 7; radius: 4
                                    color: statusColor
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                // Timestamp
                                Text {
                                    text: model.tsStr
                                    color: root.textMuted
                                    font.pixelSize: 10
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                // Source badge
                                Rectangle {
                                    visible: (model.source || "").length > 0
                                    height: 15
                                    width: sourceBadgeText.implicitWidth + 8
                                    radius: 3
                                    color: "#2A2A3A"
                                    Layout.alignment: Qt.AlignVCenter

                                    Text {
                                        id: sourceBadgeText
                                        anchors.centerIn: parent
                                        text: model.source || ""
                                        color: root.textSecondary
                                        font.pixelSize: 9
                                    }
                                }

                                // Label + CID
                                Text {
                                    id: msgText
                                    Layout.fillWidth: true
                                    text: {
                                        var cid = model.cid.length > 0
                                                   ? model.cid.substring(0, 12) + "..."
                                                   : "unknown"
                                        return model.label + " — " + cid
                                    }
                                    color: statusColor
                                    font.pixelSize: 11
                                    wrapMode: Text.Wrap
                                }

                                // Per-row copy icon (on hover)
                                Item {
                                    width: 16; height: 16
                                    Layout.alignment: Qt.AlignVCenter
                                    opacity: rowArea.containsMouse ? 1.0 : 0.0
                                    Behavior on opacity { NumberAnimation { duration: 150 } }

                                    Rectangle {
                                        x: 0; y: 2; width: 10; height: 11
                                        color: "transparent"
                                        border.color: root.textSecondary; border.width: 1; radius: 1
                                    }
                                    Rectangle {
                                        x: 3; y: 0; width: 10; height: 11
                                        color: rowArea.containsMouse ? root.bgSecondary : root.bgPrimary
                                        border.color: root.textSecondary; border.width: 1; radius: 1
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            root.copyToClipboard(JSON.stringify({
                                                cid: model.cid, inscriptionId: model.inscriptionId,
                                                label: model.label, source: model.source,
                                                status: model.status, channel: root.channelId
                                            }))
                                        }
                                    }
                                }
                            }

                            MouseArea {
                                id: rowArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    root.copyToClipboard(JSON.stringify({
                                        cid: model.cid, inscriptionId: model.inscriptionId,
                                        label: model.label, source: model.source,
                                        status: model.status, channel: root.channelId
                                    }))
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
