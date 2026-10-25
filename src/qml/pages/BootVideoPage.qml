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
            id: bootPath
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
                        Layout.preferredHeight: 34
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
                        text: qsTr("The patcher writes a small player next to HD-Player.exe and points the "
                                   + "player's own boot screen at it, so your video is tried with Qt6, Qt5 "
                                   + "and WebEngine media at run time. The EXE itself never grows. Close "
                                   + "BlueStacks first; the original .bak is preserved.")
                    }
                }

                Stepper {
                    id: bootHold
                    Layout.fillWidth: true
                    label: qsTr("Hold the video on screen after boot")
                    value: 3000
                    from: 0
                    to: 10000
                    step: 500
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.textDim
                    font.pixelSize: 12
                    text: bootHold.value > 0
                          ? qsTr("The player keeps its boot screen, and so the video, on screen for "
                                 + bootHold.value + " ms after boot finishes.")
                          : qsTr("0 ms: the player hides its boot screen as soon as boot finishes.")
                }

                RowLayout {
                    Layout.fillWidth: true
                    TextArea {
                        Layout.fillWidth: true
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        text: qsTr("State: ") + page.state
                        color: Theme.logColor(page.stateColorName)
                        font.pixelSize: 13
                    }
                    ActionButton {
                        accent: true
                        text: qsTr("Patch Boot Video")
                        onClicked: {
                            backend.exePath = bootPath.text;
                            const r = backend.bootPatch(bootHold.value);
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
