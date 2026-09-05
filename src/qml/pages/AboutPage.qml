import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Item {
    id: page

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Text {
            text: qsTr("About")
            color: Theme.text
            font.pixelSize: 24
            font.weight: Font.Bold
        }

        // ---------------- app card ----------------
        Card {
            Layout.fillWidth: true
            pad: 18

            RowLayout {
                anchors.fill: parent
                spacing: 16

                Rectangle {
                    Layout.preferredWidth: 64
                    Layout.preferredHeight: 64
                    Layout.alignment: Qt.AlignTop
                    radius: 16
                    color: Theme.accent
                    Text {
                        anchors.centerIn: parent
                        text: "BSI"
                        color: "#ffffff"
                        font.pixelSize: 18
                        font.weight: Font.Black
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: qsTr("BSI Customizer")
                        color: Theme.text
                        font.pixelSize: 20
                        font.weight: Font.Bold
                    }
                    Text {
                        text: qsTr("Version %1").arg(settings.version)
                        color: Theme.textDim
                        font.pixelSize: 12
                        font.family: "Consolas"
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        wrapMode: Text.Wrap
                        text: qsTr("A native C++ / Qt 6 modding toolkit for BlueStacks 5 and "
                                   + "MSI App Player 5. Patches HD-Player.exe and "
                                   + "libOpenglRender.dll directly — theme colors, SVG icons, "
                                   + "FPS cap, integrity check, boot video and more. Every patch "
                                   + "writes a .bak backup first and verifies before writing.")
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                }
            }
        }

        // ---------------- credits card ----------------
        Card {
            Layout.fillWidth: true
            pad: 18

            ColumnLayout {
                anchors.fill: parent
                spacing: 12

                Text {
                    text: qsTr("Made By")
                    color: Theme.text
                    font.pixelSize: 15
                    font.weight: Font.Bold
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Repeater {
                        model: [
                            { name: "Romulos",     role: qsTr("Developer") },
                            { name: "Mr.Pc Gamer", role: qsTr("Developer") }
                        ]
                        delegate: Rectangle {
                            id: authorCard
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: 72
                            radius: 10
                            color: Theme.raised
                            border.width: 1
                            border.color: Theme.border

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 14
                                anchors.rightMargin: 14
                                spacing: 12

                                Rectangle {
                                    Layout.preferredWidth: 42
                                    Layout.preferredHeight: 42
                                    radius: 21
                                    color: Theme.accentSoft
                                    border.width: 1
                                    border.color: Theme.accent
                                    Text {
                                        anchors.centerIn: parent
                                        text: authorCard.modelData.name.charAt(0)
                                        color: Theme.accentText
                                        font.pixelSize: 18
                                        font.weight: Font.Black
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        text: authorCard.modelData.name
                                        color: Theme.text
                                        font.pixelSize: 14
                                        font.weight: Font.Bold
                                    }
                                    Text {
                                        text: authorCard.modelData.role
                                        color: Theme.textDim
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---------------- notes card ----------------
        Card {
            Layout.fillWidth: true
            pad: 18

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Text {
                    text: qsTr("Notes")
                    color: Theme.text
                    font.pixelSize: 15
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: qsTr("• Close BlueStacks (HD-Player.exe) before patching, and run this "
                               + "tool as administrator.\n"
                               + "• Every patcher creates a .bak next to the original file the "
                               + "first time it writes; Restore rolls back byte-identically.\n"
                               + "• If the Theme page cannot find the theme, run "
                               + "\u201CBSI-Customizer.exe --theme-probe <path to HD-Player.exe>\u201D "
                               + "from a terminal and it will print exactly what detection sees.")
                    color: Theme.textDim
                    font.pixelSize: 12
                    lineHeight: 1.35
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
