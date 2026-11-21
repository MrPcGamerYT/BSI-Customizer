pragma Singleton
import QtQuick 2.15
QtObject {
    readonly property bool motionEnabled: true
    readonly property int duration: 160
    readonly property int corner: 12
    readonly property string fontFamily: "Segoe UI"
    readonly property color background: "#080809"
    readonly property color surface: "#111116"
    readonly property color raised: "#23232d"
    readonly property color border: "#353542"
    readonly property color text: "#eeeeF4"
    readonly property color muted: "#a0a0b0"
    readonly property color accent: "#eeeef4"
    readonly property color accent_hover: "#eeeeF4"
    readonly property color accent_pressed: "#c4c4cc"
    readonly property color danger: "#ef5364"
    readonly property color success: "#49bd88"
    readonly property color warning: "#e6b85c"
    readonly property color overlay_black: "#99080809"
    readonly property color overlay_bright: "#1feeeeF4"
    readonly property color overlay_tint: "#66111116"
    readonly property color overlay_primary: "#cc080809"
    readonly property color accent_transparent: "#44eeeef4"
}
