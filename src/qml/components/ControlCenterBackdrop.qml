import QtQuick
import BSICustomizer

Item {
    id: backdrop
    property bool animationAllowed: true
    readonly property bool moving: animationAllowed && visible && Theme.backgroundMotion && !Theme.reduceMotion
    clip: true

    // Two small, cached textures: animate scene-graph transforms, never repaint
    // a window-sized Canvas every frame. No blur/shader dependencies or timers.
    Repeater {
        model: 2
        delegate: Canvas {
            id: glow
            required property int index
            width: Math.min(720, backdrop.width * 0.7); height: width
            x: index === 0 ? backdrop.width - width * 0.8 : -width * 0.2
            y: index === 0 ? -height * 0.5 : backdrop.height - height * 0.7
            opacity: Theme.dark ? 0.18 : 0.08
            onWidthChanged: requestPaint()
            Connections { target: Theme; function onAccentChanged() { glow.requestPaint(); } }
            onPaint: {
                var c = getContext("2d"); c.clearRect(0, 0, width, height);
                var g = c.createRadialGradient(width / 2, height / 2, 0, width / 2, height / 2, width / 2);
                g.addColorStop(0, Theme.accent); g.addColorStop(1, Qt.alpha(Theme.accent, 0));
                c.fillStyle = g; c.fillRect(0, 0, width, height);
            }
            transform: Translate {
                id: drift
                objectName: "ambientDrift" + glow.index
                SequentialAnimation on x {
                    running: backdrop.moving; loops: Animation.Infinite
                    NumberAnimation { from: 0; to: glow.index === 0 ? -100 : 100; duration: 14000; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 0; duration: 14000; easing.type: Easing.InOutSine }
                }
            }
        }
    }
    Repeater {
        model: 12
        delegate: Rectangle {
            id: mote
            required property int index
            width: index % 3 === 0 ? 3 : 2; height: width; radius: width / 2
            x: backdrop.width * ((index * 73 % 97) / 100)
            y: backdrop.height * ((index * 31 % 89) / 100)
            color: Theme.accent
            opacity: Theme.dark ? 0.22 : 0.12
            visible: Theme.backgroundMotion && !Theme.reduceMotion
            transform: Translate {
                SequentialAnimation on y {
                    running: backdrop.moving; loops: Animation.Infinite
                    NumberAnimation { from: 0; to: -35 - mote.index * 3; duration: 7000 + mote.index * 530; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 0; duration: 7000 + mote.index * 530; easing.type: Easing.InOutSine }
                }
            }
        }
    }
}
