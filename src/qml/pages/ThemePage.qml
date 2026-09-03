import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Item {
    id: page

    property bool qmlMethod: methodChips.currentIndex === 0

    function editCount() {
        let n = 0;
        for (const e of backend.themeEntries)
            if (e.edit !== "")
                n++;
        return n;
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            ColumnLayout {
                spacing: 2
                Text {
                    text: qsTr("Theme Changer")
                    color: Theme.text
                    font.pixelSize: 24
                    font.weight: Font.Bold
                }
                Text {
                    text: backend.themeStatus
                    color: Theme.textDim
                    font.pixelSize: 12
                }
            }
            Item { Layout.fillWidth: true }
            ChoiceChips {
                id: methodChips
                model: [qsTr("QML (new)"), qsTr("Hex (old)")]
                onSelected: (index) => backend.loadTheme(index === 0 ? "qml" : "hex")
            }
        }

        PathRow {
            Layout.fillWidth: true
            label: qsTr("HD-Player.exe")
            showLoad: true
            text: backend.exePath
            onLoad: {
                backend.exePath = text;
                backend.loadTheme(page.qmlMethod ? "qml" : "hex");
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: page.qmlMethod ? 0 : 1

            // ---------------- QML (zlib) method ----------------
            ColumnLayout {
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("%1 colors loaded · %2 edited")
                              .arg(backend.themeEntries.length)
                              .arg(page.editCount())
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                    ActionButton {
                        text: qsTr("Clear Edits")
                        onClicked: backend.clearThemeEdits()
                    }
                    ActionButton {
                        accent: true
                        text: qsTr("Patch Theme")
                        onClicked: backend.patchThemeQml()
                    }
                }
                ListView {
                    id: qmlList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: backend.themeEntries
                    spacing: 4
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        width: qmlList.width
                        height: 42
                        radius: 8
                        color: Theme.card
                        border.color: modelData.edit !== "" ? Theme.accent : Theme.border

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 10
                            Text {
                                Layout.preferredWidth: 190
                                elide: Text.ElideRight
                                text: modelData.name
                                color: Theme.text
                                font.pixelSize: 12
                                font.family: "Consolas"
                            }
                            Text {
                                Layout.preferredWidth: 70
                                text: modelData.value
                                color: Theme.textDim
                                font.pixelSize: 12
                                font.family: "Consolas"
                            }
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: modelData.purpose
                                color: Theme.textDim
                                font.pixelSize: 11
                            }
                            ColorField {
                                current: modelData.value
                                value: modelData.edit
                                onEdited: (hex) => backend.setThemeEdit(modelData.name, hex)
                            }
                        }
                    }
                }
            }

            // ---------------- Hex (legacy) method ----------------
            ColumnLayout {
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Legacy palette — replaces color constants directly in the binary.")
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                    ActionButton {
                        text: qsTr("Reset All")
                        onClicked: backend.resetHexColors()
                    }
                    ActionButton {
                        text: qsTr("Preview")
                        onClicked: backend.previewHex()
                    }
                    ActionButton {
                        accent: true
                        text: qsTr("Apply Theme")
                        onClicked: backend.applyHex()
                    }
                }
                ListView {
                    id: hexList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: backend.hexColors
                    spacing: 4
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        width: hexList.width
                        height: 42
                        radius: 8
                        color: Theme.card
                        border.color: modelData.enabled ? Theme.accent : Theme.border

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 10

                            Rectangle {
                                width: 18
                                height: 18
                                radius: 5
                                color: modelData.enabled ? Theme.accent : "transparent"
                                border.width: 1
                                border.color: Theme.border
                                Text {
                                    anchors.centerIn: parent
                                    visible: modelData.enabled
                                    text: "\u2713"
                                    color: "#ffffff"
                                    font.pixelSize: 11
                                    font.weight: Font.Black
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: backend.setHexEnabled(modelData.name, !modelData.enabled)
                                }
                            }

                            Text {
                                Layout.preferredWidth: 190
                                elide: Text.ElideRight
                                text: modelData.name
                                color: Theme.text
                                font.pixelSize: 12
                                font.family: "Consolas"
                            }
                            Text {
                                Layout.preferredWidth: 70
                                text: modelData.current
                                color: Theme.textDim
                                font.pixelSize: 12
                                font.family: "Consolas"
                            }
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: modelData.purpose
                                color: Theme.textDim
                                font.pixelSize: 11
                            }
                            ColorField {
                                current: modelData.current
                                value: modelData.color !== modelData.current ? modelData.color : ""
                                onEdited: (hex) => backend.setHexColor(modelData.name, hex)
                            }
                        }
                    }
                }
            }
        }
    }
}
