import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Item {
    id: page

    property string status: qsTr("Pick the emulator executable to begin.")
    property color statusColor: Theme.textDim
    property var info: ({})
    property var nameInfo: ({})
    property bool busy: false

    readonly property string currentId: info && info.currentId ? info.currentId : ""
    readonly property bool patched: info ? info.patched === true : false
    readonly property bool hasBackup: info ? info.hasBackup === true : false
    readonly property int leftoverCount: info && info.leftoverCount ? info.leftoverCount : 0
    readonly property string appName: info && info.name ? info.name : (info && info.known ? info.known : "")
    readonly property var nameEntries: nameInfo && nameInfo.entries ? nameInfo.entries : []
    readonly property int nameLimit: {
        var limit = 0
        for (var i = 0; i < page.nameEntries.length; ++i) {
            var max = page.nameEntries[i].max
            if (limit === 0 || max < limit)
                limit = max
        }
        return limit
    }
    readonly property bool validNew: /^[0-9]{17,20}$/.test(newId.text.trim())
    readonly property bool sameAsCurrent: page.validNew && newId.text.trim() === page.currentId

    function refresh() {
        if (!exePath.text) { page.info = ({}); page.nameInfo = ({}); return; }
        page.info = backend.rpcidStatus(exePath.text)
        page.nameInfo = backend.rpcidNames(exePath.text)
    }

    function showResult(result) {
        page.status = result.msg || qsTr("Finished.")
        page.statusColor = result.ok ? Theme.logColor("green") : Theme.danger
        page.refresh()
    }

    function run(fn) {
        page.busy = true
        backend.exePath = exePath.text
        page.showResult(fn())
        page.busy = false
    }

    Component.onCompleted: page.refresh()

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 16

            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 24
                Layout.bottomMargin: 0
                spacing: 3
                Text {
                    text: qsTr("Discord Rich Presence")
                    color: Theme.text
                    font.pixelSize: 24
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: qsTr("Replace the Discord Application ID embedded in the emulator executable. "
                               + "Already patched files are remembered, so you can change the ID as often as you like.")
                    color: Theme.textDim
                    font.pixelSize: 12
                }
            }

            // ---- 1. file ----------------------------------------------------
            Card {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12

                    Text {
                        text: qsTr("1 · Executable")
                        color: Theme.text
                        font.pixelSize: 15
                        font.weight: Font.Bold
                    }

                    PathRow {
                        id: exePath
                        Layout.fillWidth: true
                        label: qsTr("Emulator executable")
                        text: backend.exePath
                        onLoad: {
                            backend.exePath = text
                            page.refresh()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        CheckBox {
                            id: offline
                            text: qsTr("Offline scan (do not query Discord)")
                            checked: true
                            palette.text: Theme.text
                        }
                        Item { Layout.fillWidth: true }
                        ActionButton {
                            text: qsTr("Rescan")
                            enabled: !page.busy && exePath.text.length > 0
                            onClicked: page.refresh()
                        }
                        ActionButton {
                            text: qsTr("Find Application ID")
                            enabled: !page.busy && exePath.text.length > 0
                            onClicked: page.run(function () {
                                return backend.rpcidFind(exePath.text, offline.checked)
                            })
                        }
                    }
                }
            }

            // ---- 2. current state -------------------------------------------
            Card {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Text {
                            text: qsTr("2 · Current Application ID")
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.Bold
                        }
                        Item { Layout.fillWidth: true }
                        Rectangle {
                            visible: exePath.text.length > 0
                            radius: 11
                            implicitHeight: 22
                            implicitWidth: badge.implicitWidth + 20
                            color: page.patched ? (page.leftoverCount > 0 ? Qt.rgba(0.85, 0.6, 0.1, 0.18)
                                                                          : Qt.rgba(0.2, 0.75, 0.4, 0.18))
                                                : Qt.rgba(1, 1, 1, 0.06)
                            border.color: page.patched ? (page.leftoverCount > 0 ? Theme.logColor("orange")
                                                                                  : Theme.logColor("green"))
                                                       : Theme.border
                            Text {
                                id: badge
                                anchors.centerIn: parent
                                text: page.patched ? (page.leftoverCount > 0 ? qsTr("Patched (incomplete)")
                                                                             : qsTr("Patched"))
                                                   : qsTr("Stock / unknown")
                                color: page.patched ? (page.leftoverCount > 0 ? Theme.logColor("orange")
                                                                              : Theme.logColor("green"))
                                                    : Theme.textDim
                                font.pixelSize: 11
                                font.weight: Font.Medium
                            }
                        }
                    }

                    TextInput {
                        Layout.fillWidth: true
                        readOnly: true
                        selectByMouse: true
                        text: page.currentId ? page.currentId : qsTr("—")
                        color: page.currentId ? Theme.text : Theme.textDim
                        font.pixelSize: 20
                        font.family: "Consolas"
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        wrapMode: Text.Wrap
                        font.pixelSize: 12
                        color: Theme.textDim
                        text: {
                            var parts = []
                            if (page.appName) parts.push(qsTr("Application: ") + page.appName)
                            if (page.info && page.info.originalId)
                                parts.push(qsTr("Original ID: ") + page.info.originalId)
                            if (page.hasBackup) parts.push(qsTr("Backup available"))
                            return parts.join("   ·   ")
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: page.leftoverCount > 0
                        wrapMode: Text.Wrap
                        font.pixelSize: 12
                        color: Theme.logColor("orange")
                        text: qsTr("Leftover copies of an older ID remain (their slot was too small for the last patch). "
                                   + "Patching again with a shorter ID repairs them, or use Undo to restore the original file.")
                    }
                }
            }

            // ---- 3. replace ---------------------------------------------------
            Card {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12

                    Text {
                        text: qsTr("3 · New Application ID")
                        color: Theme.text
                        font.pixelSize: 15
                        font.weight: Font.Bold
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        TextField {
                            id: newId
                            Layout.fillWidth: true
                            placeholderText: qsTr("17–20 digit Discord Application ID")
                            color: Theme.text
                            font.family: "Consolas"
                            font.pixelSize: 13
                            selectByMouse: true
                            placeholderTextColor: Theme.textDim
                            validator: RegularExpressionValidator { regularExpression: /[0-9]{0,20}/ }
                            background: Rectangle {
                                color: Theme.raised
                                radius: 7
                                border.color: newId.text.length === 0
                                              ? (newId.activeFocus ? Theme.accent : Theme.border)
                                              : (page.validNew ? Theme.logColor("green") : Theme.danger)
                            }
                        }
                        ActionButton {
                            accent: true
                            text: page.busy ? qsTr("Working…") : qsTr("Replace ID")
                            enabled: !page.busy && page.validNew && exePath.text.length > 0
                            onClicked: page.run(function () {
                                return backend.rpcidReplace(exePath.text, newId.text.trim(),
                                                            oldId.text.trim(), false, offline.checked)
                            })
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.pixelSize: 11
                        visible: newId.text.length > 0 || page.currentId.length > 0
                        color: (newId.text.length > 0 && !page.validNew) ? Theme.danger : Theme.textDim
                        text: newId.text.length > 0 && !page.validNew
                              ? qsTr("An Application ID is 17 to 20 digits.")
                              : (page.sameAsCurrent
                                 ? qsTr("That is already the ID in this file — patching again just re-confirms it.")
                                 : (page.patched
                                    ? (page.leftoverCount > 0
                                       ? qsTr("This file is already patched; the current ID above will be replaced and any leftover copies the new ID fits are repaired.")
                                       : qsTr("This file is already patched; the current ID above will be replaced."))
                                    : qsTr("The detected ID above will be replaced.")))
                    }

                    // Advanced -------------------------------------------------
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        CheckBox {
                            id: advanced
                            text: qsTr("Advanced: override the ID to search for")
                            palette.text: Theme.textDim
                        }
                        Item { Layout.fillWidth: true }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: advanced.checked
                        spacing: 10
                        Text {
                            text: qsTr("Old ID")
                            color: Theme.textDim
                            font.pixelSize: 12
                            Layout.preferredWidth: 60
                        }
                        TextField {
                            id: oldId
                            Layout.fillWidth: true
                            placeholderText: qsTr("Leave empty to use the detected / previously patched ID")
                            color: Theme.text
                            font.family: "Consolas"
                            selectByMouse: true
                            placeholderTextColor: Theme.textDim
                            validator: RegularExpressionValidator { regularExpression: /[0-9]{0,20}/ }
                            background: Rectangle { color: Theme.raised; radius: 7; border.color: Theme.border }
                        }
                        ActionButton {
                            text: qsTr("Use current")
                            enabled: page.currentId.length > 0
                            onClicked: oldId.text = page.currentId
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Item { Layout.fillWidth: true }
                        ActionButton {
                            text: qsTr("Undo Last Change")
                            enabled: !page.busy && page.hasBackup
                            onClicked: page.run(function () {
                                return backend.rpcidUndo(exePath.text)
                            })
                        }
                    }
                }
            }

            // ---- 4. window / Explorer name ----------------------------------
            Card {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    Text {
                        text: qsTr("4 · Windows name (optional)")
                        color: Theme.text
                        font.pixelSize: 15
                        font.weight: Font.Bold
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.pixelSize: 12
                        color: Theme.textDim
                        text: qsTr("The name Windows shows for this executable in Explorer, Task Manager "
                                   + "and most overlays. It is stored in a fixed-size slot, so a new name "
                                   + "must be no longer than the limit below. This does not rename the "
                                   + "Discord application itself.")
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        visible: exePath.text.length > 0 && page.nameEntries.length === 0
                        font.pixelSize: 12
                        color: Theme.textDim
                        text: page.nameInfo && page.nameInfo.msg ? page.nameInfo.msg
                                                                 : qsTr("No editable name was found in this file.")
                    }

                    Repeater {
                        model: page.nameEntries
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 10
                            Text {
                                Layout.preferredWidth: 130
                                text: modelData.key
                                color: Theme.textDim
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.value
                                color: Theme.text
                                font.pixelSize: 12
                                font.family: "Consolas"
                                elide: Text.ElideRight
                            }
                            Text {
                                text: qsTr("max %1").arg(modelData.max)
                                color: Theme.textDim
                                font.pixelSize: 11
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        TextField {
                            id: newName
                            Layout.fillWidth: true
                            enabled: page.nameEntries.length > 0
                            placeholderText: qsTr("New name, e.g. My Player")
                            color: Theme.text
                            font.pixelSize: 13
                            selectByMouse: true
                            placeholderTextColor: Theme.textDim
                            maximumLength: page.nameLimit > 0 ? page.nameLimit : 32767
                            background: Rectangle {
                                color: Theme.raised
                                radius: 7
                                border.color: newName.activeFocus ? Theme.accent : Theme.border
                            }
                        }
                        ActionButton {
                            text: qsTr("Apply Name")
                            enabled: !page.busy && newName.text.trim().length > 0
                                     && page.nameEntries.length > 0
                            onClicked: page.run(function () {
                                return backend.rpcidSetName(exePath.text, newName.text.trim(), "", false)
                            })
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.pixelSize: 11
                        color: Theme.textDim
                        visible: page.nameLimit > 0
                        text: qsTr("FileDescription and ProductName are written in every translation the file "
                                   + "carries; the shortest slot holds %1 characters. The file size never "
                                   + "changes and the original is kept as the same .rpcid-bak backup, which "
                                   + "Undo restores.").arg(page.nameLimit)
                    }
                }
            }

            // ---- output ---------------------------------------------------
            Card {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.bottomMargin: 24
                Layout.preferredHeight: 230
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10
                    Text {
                        text: qsTr("Output")
                        color: Theme.text
                        font.pixelSize: 15
                        font.weight: Font.Bold
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Theme.raised
                        radius: 8
                        border.color: Theme.border
                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: 10
                            clip: true
                            TextArea {
                                readOnly: true
                                selectByMouse: true
                                wrapMode: TextArea.Wrap
                                text: page.status
                                color: page.statusColor
                                font.pixelSize: 12
                                font.family: "Consolas"
                                background: null
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.pixelSize: 11
                        color: Theme.textDim
                        text: qsTr("Every stored form of the ID is replaced in place, so the file size never changes. "
                                   + "The original file is backed up the first time and the current ID is remembered, "
                                   + "so you can change it again later as often as you like. "
                                   + "Close the emulator before replacing an ID.")
                    }
                }
            }
        }
    }
}
