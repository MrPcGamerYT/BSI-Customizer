import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Item {
    id: page

    signal notify(string message)

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: content
            width: parent.width - 44
            x: 22
            y: 18
            spacing: 14

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    text: qsTr("Appearance")
                    color: Theme.text
                    font.pixelSize: 18
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Set the app's look and choose an accent that carries through the interface.")
                    color: Theme.textDim
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
            }

            // Three clear mode cards replace the small legacy chip row.
            Card {
                Layout.fillWidth: true
                pad: 14
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: qsTr("Color mode")
                            color: Theme.text
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: themes.theme === 0 ? qsTr("Dark")
                                  : themes.theme === 1 ? qsTr("Light") : qsTr("System")
                            color: Theme.accentText
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 9
                        Repeater {
                            model: [
                                { label: qsTr("Dark"), hint: qsTr("Deep contrast") },
                                { label: qsTr("Light"), hint: qsTr("Bright surfaces") },
                                { label: qsTr("System"), hint: qsTr("Follow Windows") }
                            ]
                            delegate: Rectangle {
                                id: modeCard
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                Layout.preferredHeight: 72
                                radius: 11
                                color: themes.theme === index ? Theme.accentSoft
                                       : modeMouse.containsMouse ? Theme.raised : Theme.cardLow
                                border.width: 1
                                border.color: themes.theme === index ? Theme.borderStrong : Theme.border
                                Behavior on color { ColorAnimation { duration: 120 } }
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 5
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text {
                                            text: modeCard.modelData.label
                                            color: themes.theme === modeCard.index ? Theme.accentText : Theme.text
                                            font.pixelSize: 12
                                            font.weight: Font.DemiBold
                                        }
                                        Item { Layout.fillWidth: true }
                                        Rectangle {
                                            Layout.preferredWidth: 13
                                            Layout.preferredHeight: 13
                                            radius: 7
                                            color: "transparent"
                                            border.width: 1
                                            border.color: themes.theme === modeCard.index ? Theme.accent : Theme.borderStrong
                                            Rectangle {
                                                anchors.centerIn: parent
                                                width: 7
                                                height: 7
                                                radius: 4
                                                visible: themes.theme === modeCard.index
                                                color: Theme.accent
                                            }
                                        }
                                    }
                                    Text {
                                        text: modeCard.modelData.hint
                                        color: Theme.textDim
                                        font.pixelSize: 10
                                    }
                                }
                                MouseArea {
                                    id: modeMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: themes.theme = modeCard.index
                                }
                            }
                        }
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                pad: 14
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 11
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: qsTr("Accent color")
                                color: Theme.text
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: qsTr("Used for active tabs, focus rings and primary actions.")
                                color: Theme.textDim
                                font.pixelSize: 10
                            }
                        }
                        Rectangle {
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 28
                            radius: 9
                            color: themes.accentColor
                            border.width: 1
                            border.color: Theme.borderStrong
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 4
                        rowSpacing: 8
                        columnSpacing: 8
                        Repeater {
                            model: ["#23e3ff", "#4dffd9", "#4f8cff", "#9d7bff",
                                    "#ffc45c", "#ff6f83", "#00c2ff", "#f5d76e"]
                            delegate: Rectangle {
                                required property string modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 30
                                radius: 9
                                color: modelData
                                border.width: Theme.hexFromColor(themes.accentColor) === modelData.toUpperCase() ? 2 : 1
                                border.color: Theme.hexFromColor(themes.accentColor) === modelData.toUpperCase() ? Theme.text : Theme.border
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 9
                                    height: 9
                                    radius: 5
                                    visible: Theme.hexFromColor(themes.accentColor) === modelData.toUpperCase()
                                    color: "#ffffff"
                                    border.width: 1
                                    border.color: "#202b3b"
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: themes.accentColor = modelData
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            text: qsTr("Custom hex")
                            color: Theme.textDim
                            font.pixelSize: 10
                        }
                        TextField {
                            id: customHex
                            Layout.preferredWidth: 120
                            placeholderText: "#RRGGBB"
                            text: Theme.hexFromColor(themes.accentColor)
                            color: Theme.text
                            placeholderTextColor: Theme.textDim
                            selectionColor: Theme.accent
                            font.family: "Consolas"
                            font.pixelSize: 11
                            leftPadding: 10
                            rightPadding: 10
                            background: Rectangle {
                                color: Theme.cardLow
                                radius: 8
                                border.width: 1
                                border.color: customHex.activeFocus ? Theme.borderStrong : Theme.border
                            }
                            onAccepted: {
                                if (/^#?[0-9A-Fa-f]{6}$/.test(text)) {
                                    themes.accentColor = text.startsWith("#") ? text : "#" + text;
                                    page.notify(qsTr("Accent color saved."));
                                } else {
                                    page.notify(qsTr("Enter a six-digit hex color, such as #23E3FF."));
                                }
                            }
                        }
                        Text {
                            text: qsTr("Press Enter to apply")
                            color: Theme.textDim
                            font.pixelSize: 10
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
            }
            Item { Layout.preferredHeight: 4 }
        }
    }
}
