import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Item {
    id: page

    property string state: qsTr("Not checked yet")
    property string stateColorName: "gray"

    function setState(r) {
        page.state = r.msg || "?";
        page.stateColorName = r.ok ? (String(r.state).indexOf("patch") >= 0 ? "green" : "orange") : "red";
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Text {
            text: qsTr("Integrity Bypass")
            color: Theme.text
            font.pixelSize: 24
            font.weight: Font.Bold
        }

        PathRow {
            Layout.fillWidth: true
            label: qsTr("HD-Player.exe")
            text: backend.exePath
            onLoad: backend.exePath = text
        }

        Card {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Prevents BlueStacks from restoring modified files, so your theme, "
                                   + "SVG and FPS patches survive restarts and updates.")
                        color: Theme.textDim
                        font.pixelSize: 13
                    }
                    ActionButton {
                        text: qsTr("Check")
                        onClicked: page.setState(backend.integrityCheck())
                    }
                    ActionButton {
                        text: qsTr("Restore")
                        onClicked: page.setState(backend.integrityRestore())
                    }
                    ActionButton {
                        accent: true
                        text: qsTr("Bypass")
                        onClicked: page.setState(backend.integrityPatch())
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 40
                    Layout.preferredHeight: 40
                    radius: 8
                    color: Theme.raised
                    border.color: Theme.border
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        text: qsTr("State: ") + page.state
                        color: Theme.logColor(page.stateColorName)
                        font.pixelSize: 13
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
