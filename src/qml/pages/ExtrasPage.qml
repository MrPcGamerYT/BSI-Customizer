import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import BSICustomizer

Item {
    id: page

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Text {
            text: qsTr("Extras")
            color: Theme.text
            font.pixelSize: 24
            font.weight: Font.Bold
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            // ---------- version spoof ----------
            Card {
                Layout.fillWidth: true
                Layout.preferredHeight: 300
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    Text {
                        text: qsTr("Version Spoof")
                        color: Theme.text
                        font.pixelSize: 15
                        font.weight: Font.Bold
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Rewrite the version string inside HD-Player.exe (e.g. to fool "
                                   + "update checks). New string must not be longer than the old one.")
                        color: Theme.textDim
                        font.pixelSize: 12
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox {
                            id: versionBox
                            Layout.fillWidth: true
                            model: backend.versions
                            textRole: "version"
                            popup.background: Rectangle { color: Theme.card; border.color: Theme.border }
                            delegate: ItemDelegate {
                                required property var model
                                required property int index
                                width: versionBox.width
                                contentItem: Text {
                                    text: model.version + "  @0x" + model.offset.toString(16)
                                    color: Theme.text
                                    font.family: "Consolas"
                                    font.pixelSize: 12
                                }
                                highlighted: versionBox.highlightedIndex === index
                            }
                        }
                        ActionButton {
                            text: qsTr("Find")
                            onClicked: backend.findVersions()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            id: newVersionField
                            Layout.fillWidth: true
                            placeholderText: qsTr("New version, e.g. 5.21.0.1000")
                            color: Theme.text
                            placeholderTextColor: Theme.textDim
                            font.pixelSize: 12
                            font.family: "Consolas"
                            background: Rectangle {
                                color: Theme.raised
                                border.color: Theme.border
                                radius: 8
                            }
                        }
                        ActionButton {
                            accent: true
                            text: qsTr("Apply")
                            onClicked: {
                                if (versionBox.currentIndex >= 0)
                                    backend.applyVersion(versionBox.currentText, newVersionField.text);
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            // ---------- string removals ----------
            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Ads / UI Removal")
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.Bold
                        }
                        ActionButton {
                            text: qsTr("Restore Backup")
                            onClicked: backend.restoreBackup(backend.exePath)
                        }
                        ActionButton {
                            text: qsTr("Open Folder")
                            onClicked: settings.openExeFolder()
                        }
                        ActionButton {
                            accent: true
                            text: qsTr("Apply Removals")
                            onClicked: {
                                const names = [];
                                for (let i = 0; i < removalList.count; i++)
                                    if (removalList.itemAtIndex(i) !== null && removalList.itemAtIndex(i).checked)
                                        names.push(removalList.model.get(i).name);
                                backend.applyRemovals(names);
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Nulls out ad/UI strings in the binary so those elements never render. "
                                   + "Backup is created automatically before patching.")
                        color: Theme.textDim
                        font.pixelSize: 12
                    }

                    ListView {
                        id: removalList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 4
                        model: ListModel {}
                        ScrollBar.vertical: ScrollBar {}

                        Component.onCompleted: {
                            for (const n of backend.removalNames)
                                model.append({ name: n });
                        }

                        delegate: Rectangle {
                            id: rowRect
                            required property var model
                            required property int index
                            property bool checked: false
                            width: removalList.width
                            height: 32
                            radius: 8
                            color: Theme.raised
                            border.color: checked ? Theme.accent : Theme.border

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 8
                                Rectangle {
                                    width: 16
                                    height: 16
                                    radius: 4
                                    color: rowRect.checked ? Theme.accent : "transparent"
                                    border.width: 1
                                    border.color: Theme.border
                                    Text {
                                        anchors.centerIn: parent
                                        visible: rowRect.checked
                                        text: "\u2713"
                                        color: "#ffffff"
                                        font.pixelSize: 10
                                        font.weight: Font.Black
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: rowRect.model.name
                                    color: Theme.text
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: rowRect.checked = !rowRect.checked
                            }
                        }
                    }
                }
            }
        }
    }
}
