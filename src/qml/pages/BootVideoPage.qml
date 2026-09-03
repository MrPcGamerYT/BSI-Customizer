import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Item {
    id: page

    property string state: qsTr("Not patched yet")
    property string stateColorName: "gray"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Text {
            text: qsTr("Boot Video")
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

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text {
                        text: qsTr("Replace the BlueStacks boot animation with your own video.")
                        color: Theme.text
                        font.pixelSize: 14
                        font.weight: Font.Bold
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.textDim
                        font.pixelSize: 12
                        text: qsTr("Put your video at:")
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 34
                        radius: 8
                        color: Theme.raised
                        border.color: Theme.border
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            text: settings.exeFolder + "\\Assets\\Promo.mp4"
                            color: Theme.accentText
                            font.pixelSize: 12
                            font.family: "Consolas"
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.textDim
                        font.pixelSize: 12
                        text: qsTr("The patcher injects a video Loader into the boot QML (zlib-compressed "
                                   + "inside the EXE). A .bak backup is created before writing.")
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("State: ") + page.state
                        color: Theme.logColor(page.stateColorName)
                        font.pixelSize: 13
                    }
                    ActionButton {
                        accent: true
                        text: qsTr("Patch Boot Video")
                        onClicked: {
                            const r = backend.bootPatch();
                            page.state = r.msg || "?";
                            page.stateColorName = r.ok ? "green" : "red";
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
