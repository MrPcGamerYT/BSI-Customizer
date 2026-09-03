import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import BSICustomizer

Item {
    id: page

    property int selectedIndex: -1
    property string svgText: ""
    property bool showRaw: false

    function refreshPreview() {
        page.svgText = page.selectedIndex >= 0 ? backend.svgTextAt(page.selectedIndex) : "";
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("SVG Extract / Replace")
                color: Theme.text
                font.pixelSize: 24
                font.weight: Font.Bold
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("Extract All")
                onClicked: backend.extractSvgs()
            }
            ActionButton {
                accent: true
                text: qsTr("Scan SVGs")
                onClicked: backend.scanSvgs()
            }
        }

        PathRow {
            Layout.fillWidth: true
            label: qsTr("HD-Player.exe")
            text: backend.exePath
            onLoad: backend.exePath = text
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            ListView {
                id: svgList
                Layout.preferredWidth: 210
                Layout.fillHeight: true
                clip: true
                model: backend.svgInfos
                spacing: 4
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: svgList.width
                    height: 36
                    radius: 8
                    color: page.selectedIndex === index ? Theme.accentSoft : Theme.card
                    border.color: page.selectedIndex === index ? Theme.accent : Theme.border

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        Text {
                            text: "SVG #" + String(index).padStart(3, "0")
                            color: Theme.text
                            font.pixelSize: 12
                            font.family: "Consolas"
                        }
                        Text {
                            text: modelData.size + " bytes"
                            color: Theme.textDim
                            font.pixelSize: 10
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            page.selectedIndex = index;
                            page.refreshPreview();
                        }
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                pad: 10

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: page.selectedIndex >= 0
                                  ? qsTr("Preview — SVG #%1").arg(page.selectedIndex)
                                  : qsTr("Select an SVG on the left")
                            color: Theme.textDim
                            font.pixelSize: 12
                        }
                        ActionButton {
                            text: page.showRaw ? qsTr("Preview") : qsTr("Raw XML")
                            onClicked: page.showRaw = !page.showRaw
                        }
                        ActionButton {
                            text: qsTr("Replace…")
                            enabled: page.selectedIndex >= 0
                            onClicked: svgDialog.open()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 8
                        color: "#80808080" // checkerboard-ish neutral
                        border.color: Theme.border

                        Image {
                            id: svgImage
                            visible: !page.showRaw && page.svgText !== ""
                            anchors.fill: parent
                            anchors.margins: 14
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            source: page.svgText !== ""
                                    ? "data:image/svg+xml;utf8," + encodeURIComponent(page.svgText)
                                    : ""
                            sourceSize: Qt.size(1024, 1024)
                        }

                        Flickable {
                            visible: page.showRaw && page.svgText !== ""
                            anchors.fill: parent
                            anchors.margins: 8
                            contentHeight: rawText.implicitHeight
                            clip: true
                            TextEdit {
                                id: rawText
                                width: parent.width
                                readOnly: true
                                selectByMouse: true
                                wrapMode: TextEdit.WrapAnywhere
                                text: page.svgText
                                color: Theme.text
                                font.family: "Consolas"
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: svgDialog
        title: qsTr("Choose replacement SVG")
        nameFilters: ["SVG files (*.svg)", "All files (*)"]
        onAccepted: backend.replaceSvg(page.selectedIndex,
                                       selectedFile.toString().replace(/^file:\/{2,3}/, ""))
    }
}
