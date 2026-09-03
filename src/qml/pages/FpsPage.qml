import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Item {
    id: page

    property string state: qsTr("Not analyzed yet")
    property string stateColorName: "gray"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Text {
            text: qsTr("FPS Unlocker")
            color: Theme.text
            font.pixelSize: 24
            font.weight: Font.Bold
        }

        PathRow {
            Layout.fillWidth: true
            label: qsTr("libOpenglRender")
            text: backend.dllPath
            onLoad: backend.dllPath = text
        }

        Card {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    Stepper {
                        id: fpsStepper
                        label: qsTr("Max FPS")
                        value: 240
                        from: 60
                        to: 1440
                        step: 30
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        text: qsTr("Analyze")
                        onClicked: {
                            const r = backend.fpsAnalyze();
                            page.state = r.msg || "?";
                            page.stateColorName = r.ok ? (r.state === "patched" ? "green" : "orange") : "red";
                        }
                    }
                    ActionButton {
                        text: qsTr("Restore")
                        onClicked: {
                            const r = backend.fpsRestore();
                            page.state = r.msg || "?";
                            page.stateColorName = r.ok ? "green" : "red";
                        }
                    }
                    ActionButton {
                        accent: true
                        text: qsTr("Patch FPS")
                        onClicked: {
                            const r = backend.fpsPatch(fpsStepper.value);
                            page.state = r.msg || "?";
                            page.stateColorName = r.ok ? "green" : "red";
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 40
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

        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 6
                Text {
                    text: qsTr("How it works")
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.textDim
                    font.pixelSize: 12
                    text: qsTr("Rewrites the FPS cap logic inside libOpenglRender.dll and stores your "
                               + "limit in a free code cave. 240 Hz / 360 Hz modes are written there too, "
                               + "so every built-in FPS setting in BlueStacks honours your cap. "
                               + "A .bak backup is created automatically — use Restore to roll back.")
                }
            }
        }
    }
}
