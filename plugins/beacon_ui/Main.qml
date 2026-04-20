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
    property int    currentTab:      1   // 0 = Config, 1 = Log

    property int  stashSeenCount: 0
    property bool pollBusy:       false
    property int  inscribedCount: 0

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

    // ── Zone sequencer setup (called once at startup) ─────────────────────────
    function configureZoneSeq() {
        if (typeof logos === "undefined" || !logos.callModule) return
        if (root.signingKeyHex === "") return

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
            // set_channel_id must be called explicitly before publish() works
            logos.callModule("liblogos_zone_sequencer_module",
                             "set_channel_id", [derivedId])
            root.channelId    = derivedId
            root.zoneSeqReady = true
        }
    }

    // ── Inscription flow ──────────────────────────────────────────────────────
    function inscribeCid(cid, label) {
        if (root.pollBusy) return
        root.pollBusy = true

        var pinRaw = logos.callModule("logos_beacon", "pinCid", [cid, label])
        var pin    = callModuleParse(pinRaw)

        if (!pin || pin.error) { root.pollBusy = false; return }
        if (pin.duplicate === true) { root.pollBusy = false; return }

        var entryIndex = pin.entryIndex

        // Show pending row immediately in the UI
        var now = new Date()
        logModel.insert(entryIndex, {
            cid:           cid,
            label:         label,
            tsStr:         Qt.formatDateTime(now, "HH:mm:ss"),
            inscriptionId: "",
            status:        "pending"
        })

        var payload = JSON.stringify({
            v:     1,
            type:  "cid_pin",
            cid:   cid,
            label: label,
            ts:    Math.floor(Date.now() / 1000)
        })

        var pubRaw    = logos.callModule("liblogos_zone_sequencer_module",
                                         "publish", [payload])
        var pubResult = callModuleParse(pubRaw)

        var inscriptionId = ""
        var status        = "error"

        // Error detection: reject any string that starts with "error" (case-insensitive)
        // or objects with an error field
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

        // Update the pending row in-place directly — signal bridge not reliable
        if (entryIndex >= 0 && entryIndex < logModel.count) {
            logModel.setProperty(entryIndex, "inscriptionId", inscriptionId)
            logModel.setProperty(entryIndex, "status", status)
            if (status === "ok") root.inscribedCount++
        }

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
                inscribeCid(cid, lbl)
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
                tsStr:         Qt.formatDateTime(ts, "HH:mm:ss"),
                inscriptionId: e.inscriptionId || "",
                status:        e.status || "pending"
            })
            if (e.status === "ok") count++
        }
        root.inscribedCount = count
    }

    // ── Startup ───────────────────────────────────────────────────────────────
    Component.onCompleted: {
        if (typeof logos === "undefined" || !logos.callModule) return

        var cfgRaw = logos.callModule("logos_beacon", "getBeaconConfig", [])
        var cfg    = callModuleParse(cfgRaw)
        if (!cfg) return

        root.signingKeyHex   = cfg.signingKeyHex  || ""
        root.nodeUrl         = cfg.nodeUrl         || "http://127.0.0.1:8080"
        root.watchStash      = cfg.watchStash !== false
        root.persistencePath = cfg.persistencePath || ""

        nodeUrlInput.text = root.nodeUrl

        configureZoneSeq()
        refreshLog()
    }

    // ── Timers ────────────────────────────────────────────────────────────────
    Timer {
        id: stashPollTimer
        interval: 10000
        running:  root.watchStash
        repeat:   true
        onTriggered: root.pollStash()
    }

    Timer {
        interval: 5000
        running:  true
        repeat:   true
        onTriggered: root.refreshLog()
    }

    // ── Log model ─────────────────────────────────────────────────────────────
    ListModel { id: logModel }

    // ── UI ────────────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: root.bgPrimary

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
            }

            // ── Zone seq error banner ─────────────────────────────────────────
            Rectangle {
                visible: !root.zoneSeqReady
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

            // ── Tab bar ───────────────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 12
                spacing: 2

                Repeater {
                    model: ["Config", "Log"]
                    delegate: Rectangle {
                        height: 30
                        Layout.fillWidth: true
                        color: root.currentTab === index ? root.bgSecondary : "transparent"
                        radius: 4

                        Behavior on color { ColorAnimation { duration: 100 } }

                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            color: root.currentTab === index
                                    ? root.textPrimary : root.textSecondary
                            font.pixelSize: 13
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.currentTab = index
                        }
                    }
                }
            }

            // ── Config panel ──────────────────────────────────────────────────
            Item {
                visible: root.currentTab === 0
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.topMargin: 12
                    spacing: 14

                    // Channel ID row
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            text: "Channel ID"
                            color: root.textSecondary
                            font.pixelSize: 11
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Rectangle {
                                Layout.fillWidth: true
                                height: 32
                                color: root.bgSecondary
                                radius: 4
                                border.color: root.borderColor
                                border.width: 1

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left; anchors.leftMargin: 8
                                    anchors.right: parent.right; anchors.rightMargin: 8
                                    text: root.channelId.length > 0
                                           ? root.channelId.substring(0, 16) + "..."
                                           : "(not yet derived)"
                                    color: root.channelId.length > 0
                                            ? root.textPrimary : root.textMuted
                                    font.pixelSize: 12
                                    font.family: "monospace"
                                    elide: Text.ElideRight
                                }
                            }

                            Rectangle {
                                width: 56; height: 32
                                radius: 4
                                visible: root.channelId.length > 0
                                color: chCopyArea.pressed     ? root.accentPressed
                                     : chCopyArea.containsMouse ? root.accentHover
                                     : root.accent

                                Behavior on color { ColorAnimation { duration: 100 } }

                                Text {
                                    anchors.centerIn: parent
                                    text: "Copy"
                                    color: "#FFFFFF"
                                    font.pixelSize: 12
                                    font.bold: true
                                }

                                MouseArea {
                                    id: chCopyArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: root.copyToClipboard(root.channelId)
                                }
                            }
                        }
                    }

                    // Signing key backup row
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            text: "Signing Key Backup"
                            color: root.textSecondary
                            font.pixelSize: 11
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Rectangle {
                                Layout.fillWidth: true
                                height: 32
                                color: root.bgSecondary
                                radius: 4
                                border.color: root.borderColor
                                border.width: 1

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left; anchors.leftMargin: 8
                                    anchors.right: parent.right; anchors.rightMargin: 8
                                    text: root.signingKeyHex.length > 0
                                           ? root.signingKeyHex.substring(0, 16) + "..."
                                           : "(no key)"
                                    color: root.signingKeyHex.length > 0
                                            ? root.textPrimary : root.textMuted
                                    font.pixelSize: 12
                                    font.family: "monospace"
                                    elide: Text.ElideRight
                                }
                            }

                            Rectangle {
                                width: 56; height: 32
                                radius: 4
                                visible: root.signingKeyHex.length > 0
                                color: keyCopyArea.pressed      ? root.accentPressed
                                     : keyCopyArea.containsMouse ? root.accentHover
                                     : root.accent

                                Behavior on color { ColorAnimation { duration: 100 } }

                                Text {
                                    anchors.centerIn: parent
                                    text: "Copy"
                                    color: "#FFFFFF"
                                    font.pixelSize: 12
                                    font.bold: true
                                }

                                MouseArea {
                                    id: keyCopyArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: root.copyToClipboard(root.signingKeyHex)
                                }
                            }
                        }

                        Text {
                            text: "Warning: losing this key = losing write access to your channel"
                            color: root.warningAmber
                            font.pixelSize: 10
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                    }

                    // Node URL row
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            text: "Node URL"
                            color: root.textSecondary
                            font.pixelSize: 11
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Rectangle {
                                Layout.fillWidth: true
                                height: 32
                                color: root.bgSecondary
                                radius: 4
                                border.color: nodeUrlInput.activeFocus
                                              ? root.accent : root.borderColor
                                border.width: 1

                                Behavior on border.color { ColorAnimation { duration: 100 } }

                                TextField {
                                    id: nodeUrlInput
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    color: root.textPrimary
                                    font.pixelSize: 12
                                    font.family: "monospace"
                                    background: null
                                    leftPadding: 8
                                    placeholderText: "http://127.0.0.1:8080"
                                    placeholderTextColor: root.textMuted
                                    text: root.nodeUrl
                                }
                            }

                            Rectangle {
                                width: 56; height: 32
                                radius: 4
                                color: saveArea.pressed      ? root.accentPressed
                                     : saveArea.containsMouse ? root.accentHover
                                     : root.accent

                                Behavior on color { ColorAnimation { duration: 100 } }

                                Text {
                                    anchors.centerIn: parent
                                    text: "Save"
                                    color: "#FFFFFF"
                                    font.pixelSize: 12
                                    font.bold: true
                                }

                                MouseArea {
                                    id: saveArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        if (typeof logos === "undefined") return
                                        logos.callModule("logos_beacon", "setNodeUrl",
                                                         [nodeUrlInput.text])
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

                        Text {
                            text: "Auto-watch Stash"
                            color: root.textSecondary
                            font.pixelSize: 13
                            Layout.fillWidth: true
                        }

                        Switch {
                            id: watchStashSwitch
                            checked: root.watchStash
                            onCheckedChanged: {
                                root.watchStash = checked
                                if (typeof logos !== "undefined")
                                    logos.callModule("logos_beacon", "setWatchStash",
                                                     [checked])
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            // ── Log panel ─────────────────────────────────────────────────────
            Item {
                visible: root.currentTab === 1
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.topMargin: 8
                    spacing: 0

                    // Header row: label + copy-all
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            text: "Activity"
                            color: root.textMuted
                            font.pixelSize: 10
                            Layout.fillWidth: true
                        }

                        // Copy-all icon
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
                                        var ch = root.channelId.length > 0
                                                  ? root.channelId.substring(0, 16) + "..."
                                                  : "unknown"
                                        var verb = r.status === "error" ? "was not inscribed" : "successfully inscribed"
                                        var statusWord = r.status === "ok" ? "Confirmed"
                                                        : r.status === "error" ? "Error" : "Pending"
                                        lines.push(r.tsStr + " beacon backup " + r.label +
                                                   " with CID " + r.cid + " " + verb +
                                                   " to " + ch + " on LEZ, status: " + statusWord)
                                    }
                                    root.copyToClipboard(lines.join("\n"))
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: root.borderColor
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
                                model.status === "ok"      ? root.successGreen :
                                model.status === "error"   ? root.errorRed     :
                                                             root.warningAmber

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 4
                                anchors.rightMargin: 4
                                anchors.topMargin: 5
                                anchors.bottomMargin: 5
                                spacing: 6

                                // Status dot
                                Rectangle {
                                    width: 7; height: 7; radius: 4
                                    color: statusColor
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                // Activity message
                                Text {
                                    id: msgText
                                    Layout.fillWidth: true
                                    text: {
                                        var ch = root.channelId.length > 0
                                                  ? root.channelId.substring(0, 12) + "..."
                                                  : "unknown"
                                        var cid = model.cid.length > 0
                                                   ? model.cid.substring(0, 12) + "..."
                                                   : "unknown"
                                        var verb = model.status === "error"
                                                   ? "was not inscribed"
                                                   : "successfully inscribed"
                                        var statusWord = model.status === "ok"    ? "Confirmed"
                                                       : model.status === "error" ? "Error"
                                                       :                            "Pending"
                                        return "beacon backup " + model.label +
                                               " with CID " + cid + " " + verb +
                                               " to " + ch + " on LEZ, status: " + statusWord
                                    }
                                    color: statusColor
                                    font.pixelSize: 11
                                    wrapMode: Text.Wrap
                                }

                                // Timestamp
                                Text {
                                    text: model.tsStr
                                    color: root.textMuted
                                    font.pixelSize: 10
                                    Layout.alignment: Qt.AlignVCenter
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
                                                label: model.label, status: model.status,
                                                channel: root.channelId
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
                                        label: model.label, status: model.status,
                                        channel: root.channelId
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
