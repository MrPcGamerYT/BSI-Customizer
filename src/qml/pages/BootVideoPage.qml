import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import BSICustomizer

Item {
    id: page

    property string state: qsTr("Not patched yet")
    property string stateColorName: "gray"
    property string bootMode: "modern"
    property string videoPath: ""

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

                RowLayout {
                    Layout.fillWidth: true
                    Text { text: qsTr("Boot style"); color: Theme.text; font.weight: Font.DemiBold }
                    ComboBox {
                        id: bootStyle
                        Layout.preferredWidth: 270
                        model: [qsTr("Built-in modern boot"), qsTr("Custom MP4 + BSI overlay")]
                        currentIndex: page.bootMode === "modern" ? 0 : 1
                        onActivated: page.bootMode = currentIndex === 0 ? "modern" : "video"
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        visible: page.bootMode === "video"
                        text: qsTr("Choose MP4…")
                        onClicked: bootVideoDialog.open()
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 230
                    radius: 14
                    clip: true
                    color: "#050B14"
                    border.color: "#23405E"
                    Rectangle {
                        id: previewAura
                        width: 174; height: 174; radius: 87
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.verticalCenterOffset: -16
                        color: "#0D2038"
                        SequentialAnimation on opacity { loops: Animation.Infinite; NumberAnimation { from: 0.72; to: 0.42; duration: 1700 } NumberAnimation { to: 0.72; duration: 1700 } }
                    }
                    Rectangle {
                        width: 116; height: 116; radius: 58
                        anchors.centerIn: previewAura
                        color: "transparent"; border.width: 2; border.color: "#2588D8"
                        RotationAnimation on rotation { from: 0; to: 360; duration: 5200; loops: Animation.Infinite }
                        Text { anchors.centerIn: parent; text: "BSI"; color: "#F1FAFF"; font.pixelSize: 30; font.bold: true; font.letterSpacing: 4 }
                    }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 38; text: qsTr("STARTING YOUR PLAYER"); color: "#D8F2FF"; font.pixelSize: 12; font.bold: true; font.letterSpacing: 2 }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 19; text: page.bootMode === "modern" ? qsTr("Built-in animated boot · no extra video required") : (page.videoPath || qsTr("Choose an MP4 to preview this style on the next boot")); color: "#829CB8"; font.pixelSize: 10; elide: Text.ElideMiddle; width: parent.width - 40; horizontalAlignment: Text.AlignHCenter }
                }
                Text {
                    Layout.fillWidth: true
                    visible: page.bootMode === "video"
                    wrapMode: Text.Wrap
                    color: Theme.textDim
                    font.pixelSize: 12
                    text: qsTr("The selected MP4 is copied to Assets/Promo.mp4. The patcher preserves an existing video as Promo.mp4.bak and overlays BSI startup styling; codec support still depends on the installed Qt/WebEngine runtime.")
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.textDim
                    font.pixelSize: 12
                    text: qsTr("The patcher preserves the emulator's loading UI, validates the boot layout, refreshes QML caches, and keeps the original executable backup. Close BlueStacks before applying.")
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
                        text: page.bootMode === "modern" ? qsTr("Install Modern Boot") : qsTr("Install Custom Boot")
                        onClicked: {
                            backend.exePath = bootPath.text;
                            if (page.bootMode === "video" && page.videoPath === "") {
                                page.state = qsTr("Choose an MP4 first.");
                                page.stateColorName = "red";
                                return;
                            }
                            const r = backend.bootPatch(bootHold.value, page.bootMode, page.videoPath);
                            page.state = r.msg || "?";
                            page.stateColorName = r.ok ? "green" : "red";
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
    FileDialog {
        id: bootVideoDialog
        title: qsTr("Choose a boot MP4")
        nameFilters: [qsTr("MP4 video (*.mp4)")]
        fileMode: FileDialog.OpenFile
        onAccepted: page.videoPath = backend.localFilePath(selectedFile)
    }
}
