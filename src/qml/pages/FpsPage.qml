import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Item {
    id: page

    property string state: qsTr("Not analyzed yet")
    property string stateColorName: "gray"
    Connections {
        target: backend
        function onDllPathChanged() { fpsPath.text = backend.dllPath; }
    }

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
            id: fpsPath
            Layout.fillWidth: true
            label: qsTr("FPS slider DLL")
            text: backend.dllPath
            readOnly: settings.fpsAutoDetect
            onLoad: { settings.fpsAutoDetect = false; backend.dllPath = text; }
        }

        RowLayout {
            Layout.fillWidth: true
            StudioCheckBox {
                text: qsTr("Automatically select the Qt slider DLL for this installation")
                checked: settings.fpsAutoDetect
                onToggled: settings.fpsAutoDetect = checked
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("Auto-detect")
                onClicked: {
                    const r = backend.fpsDetect();
                    page.state = r.msg || "?";
                    page.stateColorName = r.ok ? "green" : "red";
                }
            }
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
                            if (!settings.fpsAutoDetect) backend.dllPath = fpsPath.text;
                            const r = backend.fpsAnalyze();
                            page.state = r.msg || "?";
                            page.stateColorName = r.ok ? (r.state === "patched" ? "green" : "orange") : "red";
                        }
                    }
                    ActionButton {
                        text: qsTr("Restore")
                        onClicked: {
                            if (!settings.fpsAutoDetect) backend.dllPath = fpsPath.text;
                            const r = backend.fpsRestore();
                            page.state = r.msg || "?";
                            page.stateColorName = r.ok ? "green" : "red";
                        }
                    }
                    ActionButton {
                        accent: true
                        text: qsTr("Patch FPS")
                        onClicked: {
                            if (!settings.fpsAutoDetect) backend.dllPath = fpsPath.text;
                            const r = backend.fpsPatch(fpsStepper.value);
                            page.state = r.msg || "?";
                            page.stateColorName = r.ok ? "green" : "red";
                        }
                    }
                }

                ScrollView {
                    id: statusView
                    Layout.fillWidth: true
                    contentWidth: availableWidth
                    Layout.preferredHeight: Math.max(56, Math.min(160, statusText.contentHeight + 24))
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    StudioTextArea {
                        id: statusText
                        width: statusView.availableWidth
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        padding: 12
                        text: qsTr("State: ") + page.state
                        color: Theme.logColor(page.stateColorName)
                        font.pixelSize: 13
                        background: Rectangle {
                            radius: 8
                            color: Theme.raised
                            border.color: Theme.border
                        }
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
                    text: qsTr("Auto-detection reads the selected HD-Player.exe's Qt imports and searches "
                               + "that installation for Qt5QuickTemplates2.dll or Qt6QuickTemplates2.dll. "
                               + "Use Auto-detect to search Program Files/registry if no installation is selected. "
                               + "Disable automatic selection only for a manual DLL override. "
                               + "Changes the QQuickSlider / QQuickRangeSlider upper limit from "
                               + "240 to your chosen maximum. Normal mode stays at 60 FPS; its "
                               + "value is not rewritten. Other slider values are untouched. This unlocks "
                               + "the settings slider, not a guarantee of in-game FPS. Close all "
                               + "BlueStacks instances before patching, then restart. A .bak backup "
                               + "is created automatically — use Restore to roll back.")
                }
            }
        }
    }
}
