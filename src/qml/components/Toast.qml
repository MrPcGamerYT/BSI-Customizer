import BSICustomizer
import QtQuick

Rectangle {
    id: toast

    property alias message: label.text

    function show(msg) {
        label.text = msg;
        fadeIn.stop();
        fadeOut.stop();
        hideTimer.stop();
        fadeIn.start();
        hideTimer.start();
    }

    width: Math.min(label.implicitWidth + 44, 520)
    height: 42
    radius: 9
    color: Theme.raised
    border.width: 1
    border.color: Theme.borderStrong
    opacity: 0
    visible: opacity > 0.01

    Text {
        id: label
        anchors.centerIn: parent
        width: Math.min(implicitWidth, 460)
        color: Theme.text
        font.pixelSize: 13
        elide: Text.ElideRight
    }

    NumberAnimation { id: fadeIn;  target: toast; property: "opacity"; to: 1; duration: Theme.motionDuration }
    NumberAnimation { id: fadeOut; target: toast; property: "opacity"; to: 0; duration: Theme.revealDuration }
    Timer { id: hideTimer; interval: 2400; onTriggered: fadeOut.start() }
}
