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
    property string bootLogoUrl: ""

    function refreshBootLogo() {
        const preview = backend.bootLogoPreview();
        page.bootLogoUrl = preview.logo || "";
    }

    Component.onCompleted: refreshBootLogo()
    onVisibleChanged: if (visible) refreshBootLogo()

    property string lastProfileBoot: JSON.stringify((backend.customizationProfile || {}).boot || {})
    Connections {
        target: backend
        function onCustomizationProfileChanged() {
            const b = (backend.customizationProfile || {}).boot || {};
            const value = JSON.stringify(b);
            if (value !== page.lastProfileBoot) { page.lastProfileBoot = value; bootHold.value = b.holdMs === undefined ? 3000 : b.holdMs; }
        }
        function onExePathChanged() { page.refreshBootLogo(); }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Text {
            text: qsTr("Boot Experience")
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
                        model: [qsTr("Built-in modern boot"), qsTr("Custom MP4")]
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
                        anchors.verticalCenterOffset: -10
                        color: "#183246"
                        opacity: 0.22
                        SequentialAnimation on opacity { loops: Animation.Infinite; NumberAnimation { to: 0.30; duration: 2600; easing.type: Easing.InOutSine } NumberAnimation { to: 0.16; duration: 2600; easing.type: Easing.InOutSine } }
                    }
                    Rectangle {
                        width: 122; height: 122; radius: 61
                        anchors.centerIn: previewAura
                        color: "transparent"; border.width: 1; border.color: "#648097"; opacity: 0.3
                        RotationAnimation on rotation { from: 0; to: 360; duration: 36000; loops: Animation.Infinite; easing.type: Easing.Linear }
                    }
                    Image {
                        anchors.centerIn: previewAura
                        width: 110; height: 96
                        source: page.bootLogoUrl
                        fillMode: Image.PreserveAspectFit
                        smooth: true; mipmap: true
                        opacity: 0.95
                        SequentialAnimation on scale { loops: Animation.Infinite; NumberAnimation { to: 1.018; duration: 2300; easing.type: Easing.InOutSine } NumberAnimation { to: 1.0; duration: 2300; easing.type: Easing.InOutSine } }
                    }
                    Rectangle {
                        id: previewProgressTrack
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom; anchors.bottomMargin: 23
                        width: Math.min(parent.width * 0.34, 310); height: 3; radius: 2; color: "#293D4D"
                        Rectangle {
                            id: previewProgressLight
                            width: parent.width * 0.28; height: parent.height; radius: 2; color: "#D5E7F3"
                            NumberAnimation on x { from: 0; to: previewProgressTrack.width - previewProgressLight.width; duration: 2650; loops: Animation.Infinite; easing.type: Easing.InOutSine }
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: page.bootMode === "video"
                    wrapMode: Text.Wrap
                    color: Theme.textDim
                    font.pixelSize: 12
                    text: qsTr("The selected MP4 is copied to Assets/Promo.mp4. An existing video is preserved as Promo.mp4.bak. The emulator's own logo and a quiet loading line are overlaid without labels or attribution; playback still depends on the installed Qt/WebEngine runtime.")
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.textDim
                    font.pixelSize: 12
                    text: qsTr("The selected style replaces BlueStacks' stock loading artwork and progress panel with a quiet, animated screen using the player's own logo. No extra labels or credits are shown. Native Android startup handoff and ads remain intact. The patcher validates the boot layout, refreshes QML caches, and keeps the original executable backup. Close BlueStacks before applying.")
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
                        text: page.bootMode === "modern" ? qsTr("Install Animated Modern Boot") : qsTr("Install Custom Video Boot")
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
