import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import BSICustomizer

Item {
    id: page

    property string state: qsTr("Not patched yet")
    property string stateColorName: "gray"
    property string bootMode: "aurora"
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
    ScrollView {
        id: bootScroller
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: bootBody.implicitHeight + 48
        clip: true
        ColumnLayout {
        id: bootBody
        x: 24; y: 24
        width: Math.max(0, bootScroller.availableWidth - 48)
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
                        model: [qsTr("Aurora glow"), qsTr("Orbit rings"), qsTr("Pulse wave"), qsTr("Solar flare"), qsTr("Custom MP4")]
                        currentIndex: Math.max(0, ["aurora", "orbit", "pulse", "solar", "video"].indexOf(page.bootMode))
                        onActivated: page.bootMode = ["aurora", "orbit", "pulse", "solar", "video"][currentIndex]
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        visible: page.bootMode === "video"
                        text: qsTr("Choose MP4…")
                        onClicked: bootVideoDialog.open()
                    }
                }
                Label { text: qsTr("Startup message (leave blank for default)"); color: Theme.text }
                TextField {
                    id: bootStartupText
                    Layout.fillWidth: true
                    maximumLength: 120
                    placeholderText: qsTr("Emulator is starting…")
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
                        color: page.bootMode === "solar" ? "#633C2F" : "#16496B"
                        SequentialAnimation on opacity { loops: Animation.Infinite; NumberAnimation { from: 0.18; to: 0.40; duration: page.bootMode === "pulse" ? 1800 : 2900; easing.type: Easing.InOutSine } NumberAnimation { to: 0.18; duration: page.bootMode === "pulse" ? 1800 : 2900; easing.type: Easing.InOutSine } }
                    }
                    Rectangle {
                        id: previewRingTrack
                        width: 112; height: 112; radius: 56
                        anchors.centerIn: previewAura
                        color: "transparent"; border.width: 1
                        border.color: page.bootMode === "solar" ? "#745845" : "#6689A0"
                        opacity: 0.64
                    }
                    Item {
                        visible: page.bootMode === "orbit"
                        width: previewRingTrack.width * 1.35; height: width
                        anchors.centerIn: previewRingTrack
                        RotationAnimation on rotation { from: 0; to: 360; duration: 8200; loops: Animation.Infinite; easing.type: Easing.Linear }
                        Rectangle { width: 8; height: 8; radius: 4; anchors.horizontalCenter: parent.horizontalCenter; y: -4; color: "#B8EFFF"; border.color: "white"; border.width: 1 }
                    }
                    Rectangle {
                        visible: page.bootMode === "pulse"
                        width: previewRingTrack.width; height: width; radius: width / 2
                        anchors.centerIn: previewRingTrack
                        color: "transparent"; border.width: 2; border.color: "#75D9FF"
                        SequentialAnimation on scale { loops: Animation.Infinite; NumberAnimation { from: 0.78; to: 1.5; duration: 2200; easing.type: Easing.OutCubic } }
                        SequentialAnimation on opacity { loops: Animation.Infinite; NumberAnimation { from: 0.5; to: 0; duration: 2200; easing.type: Easing.OutCubic } }
                    }
                    Canvas {
                        id: previewLoadingRing
                        objectName: "bsiLoadingRing"
                        width: previewRingTrack.width; height: width
                        anchors.centerIn: previewRingTrack
                        antialiasing: true
                        onPaint: {
                            var ctx = getContext("2d");
                            ctx.clearRect(0, 0, width, height);
                            ctx.beginPath(); ctx.lineWidth = 3; ctx.lineCap = "round";
                            ctx.strokeStyle = page.bootMode === "solar" ? "#FFE3BA" : "#B9EEFF";
                            ctx.shadowColor = page.bootMode === "solar" ? "#FFAD66" : "#55CFFF"; ctx.shadowBlur = 16;
                            ctx.arc(width / 2, height / 2, width / 2 - ctx.lineWidth, -Math.PI * 0.70, Math.PI * 0.88, false);
                            ctx.stroke();
                        }
                        RotationAnimation on rotation { from: 0; to: 360; duration: page.bootMode === "orbit" ? 4300 : page.bootMode === "pulse" ? 3600 : 3100; loops: Animation.Infinite; easing.type: Easing.Linear }
                    }
                    Rectangle {
                        visible: page.bootLogoUrl.length === 0
                        width: 58; height: 58; radius: 16
                        anchors.centerIn: previewRingTrack
                        rotation: 45
                        color: "#183246"; border.color: "#B2DFF4"
                        Text { anchors.centerIn: parent; rotation: -45; text: "BS"; color: "#D9F4FF"; font.pixelSize: 22; font.bold: true }
                    }
                    Image {
                        visible: page.bootLogoUrl.length > 0
                        anchors.centerIn: previewRingTrack
                        width: 72; height: 72
                        source: page.bootLogoUrl
                        fillMode: Image.PreserveAspectFit
                        smooth: true; mipmap: true
                        SequentialAnimation on scale { loops: Animation.Infinite; NumberAnimation { to: 1.018; duration: 2300; easing.type: Easing.InOutSine } NumberAnimation { to: 1.0; duration: 2300; easing.type: Easing.InOutSine } }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom; anchors.bottomMargin: 15
                        text: bootStartupText.text.trim().length ? bootStartupText.text : qsTr("Emulator is starting…")
                        textFormat: Text.PlainText
                        width: parent.width - 20; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                        color: "#C7D9E6"; font.pixelSize: 11; font.letterSpacing: 0.5
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: page.bootMode === "video"
                    wrapMode: Text.Wrap
                    color: Theme.textDim
                    font.pixelSize: 12
                    text: qsTr("The selected MP4 is copied to Assets/Promo.mp4. The boot page is written to Assets/BootScreen.html and loaded by HD-Player through WebEngine; that page overlays the emulator's own logo, glowing ring, and your startup message. An existing video is preserved as Promo.mp4.bak.")
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.textDim
                    font.pixelSize: 12
                    text: qsTr("The selected style writes the boot screen to Assets/BootScreen.html; the player boot page loads that HTML from disk. It shows the player's own logo, a softly glowing outer ring, and your startup message, with no BSI attribution. Native Android startup handoff and ads remain intact. The patcher validates the boot layout, refreshes QML caches, and keeps the original executable backup. Close BlueStacks before applying.")
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
                        text: page.bootMode === "video" ? qsTr("Install Custom Video Boot")
                            : page.bootMode === "orbit" ? qsTr("Install Orbit Rings Boot")
                            : page.bootMode === "pulse" ? qsTr("Install Pulse Wave Boot")
                            : page.bootMode === "solar" ? qsTr("Install Solar Flare Boot")
                            : qsTr("Install Aurora Glow Boot")
                        onClicked: {
                            backend.exePath = bootPath.text;
                            if (page.bootMode === "video" && page.videoPath === "") {
                                page.state = qsTr("Choose an MP4 first.");
                                page.stateColorName = "red";
                                return;
                            }
                            const r = backend.bootPatch(bootHold.value, page.bootMode, page.bootMode === "video" ? page.videoPath : "", bootStartupText.text);
                            page.state = r.msg || "?";
                            page.stateColorName = r.ok ? "green" : "red";
                        }
                    }
                }
            }
        }

        }
    }
    FileDialog {
        id: bootVideoDialog
        title: qsTr("Choose a boot MP4")
        nameFilters: [qsTr("MP4 video (*.mp4)")]
        fileMode: FileDialog.OpenFile
        onAccepted: page.videoPath = backend.localFilePath(selectedFile)
    }
}
