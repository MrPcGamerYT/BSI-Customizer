import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Item {
    id: dashPage
    property var instances: []

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Text {
            text: qsTr("Dashboard")
            color: Theme.text
            font.pixelSize: 24
            font.weight: Font.Bold
        }

        Card {
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: qsTr("BlueStacks Installations")
                        color: Theme.text
                        font.pixelSize: 15
                        font.weight: Font.Bold
                    }
                    Text {
                        text: qsTr("Scan the disk and registry for BlueStacks 5 / MSI 5 installs.")
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                }
                ActionButton {
                    accent: true
                    text: qsTr("Detect Installs")
                    onClicked: backend.detectInstalls()
                }
            }
        }

        Card {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(90, installsRepeater.count * 46 + 24)

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                Repeater {
                    id: installsRepeater
                    model: backend.installs
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        height: 40
                        radius: 8
                        color: Theme.raised
                        border.color: Theme.border

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 8
                            spacing: 10
                            Rectangle {
                                width: 62
                                height: 20
                                radius: 5
                                color: Theme.accentSoft
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.profile
                                    color: Theme.accentText
                                    font.pixelSize: 10
                                    font.weight: Font.Bold
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideMiddle
                                text: modelData.root
                                color: Theme.textDim
                                font.pixelSize: 12
                                font.family: "Consolas"
                            }
                            ActionButton {
                                text: qsTr("Use")
                                implicitHeight: 28
                                onClicked: backend.useInstall(index)
                            }
                        }
                    }
                }

                Text {
                    visible: installsRepeater.count === 0
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 14
                    text: qsTr("Nothing detected yet — click Detect Installs.")
                    color: Theme.textDim
                    font.pixelSize: 12
                }
            }
        }

        Card {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            ColumnLayout {
                anchors.fill: parent
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: qsTr("Target Paths")
                        color: Theme.text
                        font.pixelSize: 15
                        font.weight: Font.Bold
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        text: qsTr("Detect Instances")
                        implicitHeight: 28
                        onClicked: dashPage.instances = backend.detectInstances()
                    }
                }
                PathRow {
                    Layout.fillWidth: true
                    label: qsTr("HD-Player.exe")
                    text: backend.exePath
                    onLoad: backend.exePath = text
                }
                PathRow {
                    Layout.fillWidth: true
                    label: qsTr("libOpenglRender")
                    text: backend.dllPath
                    onLoad: backend.dllPath = text
                }
                Row {
                    spacing: 6
                    Repeater {
                        model: dashPage.instances
                        delegate: Rectangle {
                            required property var modelData
                            height: 22
                            width: chipText.implicitWidth + 16
                            radius: 6
                            color: Theme.accentSoft
                            Text {
                                id: chipText
                                anchors.centerIn: parent
                                text: modelData.display || modelData.name
                                color: Theme.accentText
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
