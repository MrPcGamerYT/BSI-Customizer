import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Item {
    id: page
    property string status: qsTr("Ready")
    property color statusColor: Theme.textDim

    function showResult(result) {
        page.status = result.msg || qsTr("rpcid finished.")
        page.statusColor = result.ok ? Theme.logColor("green") : Theme.danger
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    text: qsTr("Discord Rich Presence")
                    color: Theme.text
                    font.pixelSize: 24
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Find and replace the Discord Application ID embedded in an emulator executable.")
                    color: Theme.textDim
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
            ActionButton {
                text: qsTr("Undo Last Change")
                onClicked: page.showResult(backend.rpcidUndo(exePath.text))
            }
        }

        PathRow {
            id: exePath
            Layout.fillWidth: true
            label: qsTr("Emulator executable")
            text: backend.exePath
            onLoad: backend.exePath = text
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
                text: qsTr("Find Application ID")
                onClicked: {
                    backend.exePath = exePath.text
                    page.showResult(backend.rpcidFind(exePath.text, offline.checked))
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text { text: qsTr("New Application ID") ; color: Theme.textDim; font.pixelSize: 12 }
            TextField {
                id: newId
                Layout.fillWidth: true
                placeholderText: qsTr("17–20 digit Discord Application ID")
                color: Theme.text
                placeholderTextColor: Theme.textDim
                background: Rectangle { color: Theme.raised; radius: 7; border.color: Theme.border }
            }
            TextField {
                id: oldId
                Layout.preferredWidth: 210
                placeholderText: qsTr("Old ID (optional)")
                color: Theme.text
                placeholderTextColor: Theme.textDim
                background: Rectangle { color: Theme.raised; radius: 7; border.color: Theme.border }
            }
            ActionButton {
                accent: true
                text: qsTr("Replace ID")
                enabled: newId.text.length >= 17
                onClicked: {
                    backend.exePath = exePath.text
                    page.showResult(backend.rpcidReplace(exePath.text, newId.text, oldId.text, false, offline.checked))
                }
            }
        }

        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 12
                Text {
                    text: qsTr("How it works")
                    color: Theme.text
                    font.pixelSize: 15
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: qsTr("The tool detects Game SDK int64 values, ASCII IDs and UTF-16 IDs. "
                               + "It keeps the executable size unchanged and creates a first backup "
                               + "with the .rpcid-bak suffix. Close the emulator before replacing an ID.")
                    color: Theme.textDim
                    font.pixelSize: 12
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(70, statusLabel.implicitHeight + 20)
                    color: Theme.raised
                    radius: 8
                    border.color: Theme.border
                    Text {
                        id: statusLabel
                        anchors.fill: parent
                        anchors.margins: 10
                        text: page.status
                        color: page.statusColor
                        wrapMode: Text.Wrap
                        font.pixelSize: 12
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
