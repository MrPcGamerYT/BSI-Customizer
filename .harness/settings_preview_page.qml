// Explicit sample page for the live shell preview, NOT an emulator backend.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import BsiGuiFixture 1.0

Item {
    id: page
    property int pageIndex: 0
    property string pageTitle: "Performance"
    property bool dirty: false
    function fOnQmlLoaded() {}

    Component {
        id: choice
        ComboBox {
            implicitWidth: 204; implicitHeight: 34
            font: UiTheme.fonts.bodyMedium
            palette.text: UiTheme.colors.primary10
            palette.buttonText: UiTheme.colors.primary10
            palette.button: UiTheme.colors.primary70
            palette.base: UiTheme.colors.primary80
            palette.window: UiTheme.colors.primary80
            palette.highlight: UiTheme.colors.subWindowHeaderClick
            background: Rectangle {
                radius: 4; color: UiTheme.colors.primary70
                border.color: parent.activeFocus ? UiTheme.colors.subWindowHeaderClick : UiTheme.colors.primary60
            }
            onActivated: page.dirty = true
        }
    }
    Flickable {
        id: scroll
        x: 24; y: 12; width: parent.width - 48
        height: Math.max(0, parent.height - 74)
        clip: true; contentWidth: width; contentHeight: form.implicitHeight + 20
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Column {
            id: form; width: scroll.width; spacing: 18
            Loader {
                width: parent.width
                sourceComponent: page.pageIndex === 0 ? performance : general
            }
        }
    }
    Component {
        id: performance
        Column {
            width: parent.width; spacing: 16
            Text { text: "CPU allocation"; font: UiTheme.fonts.bodyMedium; color: UiTheme.colors.primary10 }
            Row {
                spacing: 12
                Loader { sourceComponent: choice; onLoaded: item.model = ["Custom", "Low (1 core)", "Balanced (2 cores)", "High (4 cores)"] }
                Loader { sourceComponent: choice; onLoaded: { item.width = 118; item.model = ["6 cores", "4 cores", "2 cores", "1 core"] } }
            }
            Rectangle { width: parent.width; height: 1; color: UiTheme.colors.primary60 }
            Text { text: "Memory allocation"; font: UiTheme.fonts.bodyMedium; color: UiTheme.colors.primary10 }
            Loader { sourceComponent: choice; onLoaded: item.model = ["Enhanced (4 GB)", "Standard (2 GB)", "Low (1 GB)", "Custom"] }
            Rectangle { width: parent.width; height: 1; color: UiTheme.colors.primary60 }
            Text { text: "Performance mode"; font: UiTheme.fonts.bodyMedium; color: UiTheme.colors.primary10 }
            Loader { sourceComponent: choice; onLoaded: item.model = ["High performance", "Balanced", "Low memory"] }
            Rectangle { width: parent.width; height: 1; color: UiTheme.colors.primary60 }
            Text { text: "Frame rate: " + Math.round(fps.value); font: UiTheme.fonts.bodyMedium; color: UiTheme.colors.primary10 }
            Slider {
                id: fps; width: parent.width; from: 1; to: 540; value: 144; stepSize: 1
                palette.highlight: UiTheme.colors.subWindowHeaderClick
                onMoved: page.dirty = true
            }
            Row {
                width: parent.width
                Text { text: "1"; width: parent.width / 2; color: UiTheme.colors.primary30; font: UiTheme.fonts.bodySmall }
                Text { text: "540"; width: parent.width / 2; horizontalAlignment: Text.AlignRight; color: UiTheme.colors.primary30; font: UiTheme.fonts.bodySmall }
            }
            Text {
                width: parent.width; wrapMode: Text.Wrap
                text: "Recommended FPS\nPlay at 60 FPS for smooth gameplay. A higher frame rate may require restarting the player."
                font: UiTheme.fonts.bodySmall; color: UiTheme.colors.primary30; lineHeight: 1.4
            }
            Row {
                width: parent.width
                Text { text: "Enable high frame rate"; width: parent.width - 60; anchors.verticalCenter: parent.verticalCenter; font: UiTheme.fonts.bodyMedium; color: UiTheme.colors.primary20 }
                Switch { checked: true; palette.highlight: UiTheme.colors.subWindowHeaderClick; onToggled: page.dirty = true }
            }
            Row {
                width: parent.width
                Text { text: "Enable VSync"; width: parent.width - 60; anchors.verticalCenter: parent.verticalCenter; font: UiTheme.fonts.bodyMedium; color: UiTheme.colors.primary20 }
                Switch { palette.highlight: UiTheme.colors.subWindowHeaderClick; onToggled: page.dirty = true }
            }
        }
    }
    Component {
        id: general
        Column {
            width: parent.width; spacing: 20
            property var fields: [[],
                [["Display resolution", "1920 × 1080", "1600 × 900", "1280 × 720"], ["Pixel density", "240 DPI", "320 DPI", "160 DPI"]],
                [["Graphics renderer", "OpenGL", "DirectX", "Vulkan"], ["Interface renderer", "Auto", "Software", "OpenGL"], ["ASTC textures", "Hardware decoding", "Software decoding", "Disabled"]],
                [["Game resolution", "Full HD", "HD", "QHD"], ["Graphics quality", "High", "Balanced", "Low"]],
                [["Speaker", "System default", "Headphones"], ["Microphone", "System default", "External microphone"]],
                [["Gamepad detection", "Automatic", "Always on", "Off"], ["Input settings", "Native gamepad", "On-screen controls"]],
                [["Language", "English", "Français", "Deutsch"], ["Platform", "Auto", "Android"]],
                [["Device profile", "Samsung Galaxy S22", "OnePlus 10 Pro", "Custom"]],
                [["Open Settings", "Ctrl + Shift + I", "Custom"], ["Toggle fullscreen", "F11", "Custom"]],
                [["Disk cleanup", "Ready to clean", "Scheduled"], ["Backup settings", "Manual", "Automatic"]],
                [["Debug bridge", "Disabled", "Enabled"], ["CPU architecture", "Automatic", "ARM", "x86"]]]
            Text { text: page.pageTitle; color: UiTheme.colors.primary10; font: UiTheme.fonts.bodyLarge }
            Repeater {
                model: parent.fields[page.pageIndex] || []
                Column {
                    width: parent.width; spacing: 10
                    Text { text: modelData[0]; font: UiTheme.fonts.bodyMedium; color: UiTheme.colors.primary20 }
                    Loader { sourceComponent: choice; property var options: modelData.slice(1); onLoaded: item.model = options }
                    Rectangle { width: parent.width; height: 1; color: UiTheme.colors.primary60 }
                }
            }
            Text {
                width: parent.width; wrapMode: Text.Wrap; lineHeight: 1.5
                text: "These are interactive sample controls. This preview runs the generated Settings shell; it does not read or change an emulator."
                font: UiTheme.fonts.bodySmall; color: UiTheme.colors.primary30
            }
        }
    }
    Item {
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: 46
        Rectangle { width: parent.width; height: 1; color: UiTheme.colors.primary60 }
        Text {
            x: 24; anchors.verticalCenter: parent.verticalCenter
            text: "ⓘ  Some changes will apply on next launch"
            color: UiTheme.colors.primary30; font: UiTheme.fonts.bodySmall
        }
        Button {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            width: 126; height: 32; enabled: page.dirty
            contentItem: Text {
                text: "Save changes"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                color: parent.enabled ? UiTheme.colors.primary10 : UiTheme.colors.primary30
                font: UiTheme.fonts.bodySmall
            }
            background: Rectangle {
                radius: 4; color: parent.enabled ? UiTheme.colors.primary70 : "transparent"
                border.color: parent.enabled ? UiTheme.colors.subWindowHeaderClick : UiTheme.colors.primary60
            }
            onClicked: { previewBridge.save(); page.dirty = false }
        }
    }
}
