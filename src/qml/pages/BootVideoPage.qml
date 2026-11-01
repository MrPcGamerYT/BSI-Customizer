import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Item {
    id: page

    property string state: qsTr("Not patched yet")
    property string stateColorName: "gray"

    property string lastProfileBoot: JSON.stringify((backend.customizationProfile || {}).boot || {})
    Connections {
        target: backend
        function onCustomizationProfileChanged() {
            const b = (backend.customizationProfile || {}).boot || {};
            const value = JSON.stringify(b);
            if (value !== page.lastProfileBoot) { page.lastProfileBoot = value; bootHold.value = b.holdMs === undefined ? 3000 : b.holdMs; }
        }
    }
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
                        text: qsTr("The patcher preserves the loading UI and installs an adaptive Qt 6 / Qt 5 / WebEngine player in Assets/BootVideo.qml when needed. Validated QML resources "
                                   + "can grow into mapped PE storage, and their caches are refreshed. "
                                   + "Close BlueStacks first. The original .bak is preserved.")
                    }
                }

                RowLayout {
                    Label { text: qsTr("Boot hold after ready (ms)"); color: Theme.text }
                    SpinBox { id: bootHold; from: 0; to: 15000; value: 3000; stepSize: 500; editable: true }
                    Label { text: qsTr("0 disables the extra delay"); color: Theme.textDim }
                    ActionButton { text: qsTr("Save to profile"); onClicked: { const r = backend.workspace("profile-section", {section: "boot", value: {holdMs: bootHold.value}}); page.state = r.msg || ""; } }
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
