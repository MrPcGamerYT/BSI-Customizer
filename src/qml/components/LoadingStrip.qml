import QtQuick
import BSICustomizer

// Real work only: no artificial startup delay and no input-blocking overlay.
Item {
    id: strip
    property bool busy: false
    property bool animationAllowed: true
    readonly property bool animating: busy && visible && animationAllowed && !Theme.reduceMotion
    implicitHeight: 2
    height: 2
    clip: true
    opacity: busy ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: Theme.motionDuration } }
    Rectangle { anchors.fill: parent; color: Theme.accentSoft }
    Rectangle {
        id: beam
        height: parent.height
        width: Theme.reduceMotion ? parent.width : parent.width * 0.28
        radius: 1
        color: Theme.accent
        x: Theme.reduceMotion ? 0 : -width
        NumberAnimation on x {
            running: strip.animating; loops: Animation.Infinite
            from: -beam.width; to: strip.width
            duration: 1400; easing.type: Easing.InOutSine
        }
    }
}
