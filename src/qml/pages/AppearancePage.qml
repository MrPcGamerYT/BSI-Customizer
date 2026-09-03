import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Item {
    signal notify(string message)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Text {
            text: qsTr("Appearance")
            color: Theme.text
            font.pixelSize: 24
            font.weight: Font.Bold
        }

        Card {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 12
                Text {
                    text: qsTr("App Theme")
                    color: Theme.text
                    font.pixelSize: 15
                    font.weight: Font.Bold
                }
                ChoiceChips {
                    model: [qsTr("Dark"), qsTr("Light"), qsTr("System")]
                    currentIndex: themes.theme
                    onSelected: (index) => themes.theme = index
                }
            }
        }

        Card {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 12
                Text {
                    text: qsTr("Accent Color")
                    color: Theme.text
                    font.pixelSize: 15
                    font.weight: Font.Bold
                }
                RowLayout {
                    spacing: 10
                    Repeater {
                        model: ["#7c5cff", "#5aa7ff", "#38d996", "#ffb454", "#ff6b6b",
                                "#e05cff", "#00c2ff", "#f5d76e"]
                        delegate: Rectangle {
                            required property string modelData
                            width: 34
                            height: 34
                            radius: 10
                            color: modelData
                            border.width: themes.accentColor === modelData ? 3 : 0
                            border.color: Theme.text
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: themes.accentColor = modelData
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        width: 34
                        height: 34
                        radius: 10
                        color: themes.accentColor
                        border.width: 1
                        border.color: Theme.border
                    }
                    TextField {
                        id: customHex
                        width: 110
                        placeholderText: "#RRGGBB"
                        color: Theme.text
                        placeholderTextColor: Theme.textDim
                        font.family: "Consolas"
                        font.pixelSize: 12
                        background: Rectangle {
                            color: Theme.raised
                            border.color: Theme.border
                            radius: 8
                        }
                        onAccepted: {
                            if (/^#?[0-9A-Fa-f]{6}$/.test(text)) {
                                themes.accentColor = text.startsWith("#") ? text : "#" + text;
                                notify(qsTr("Accent updated"));
                            }
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
