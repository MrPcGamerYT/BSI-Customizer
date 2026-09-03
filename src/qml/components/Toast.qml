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
    radius: 21
    color: Theme.dark ? "#e8eaf6" : "#232336"
    opacity: 0
    visible: opacity > 0.01

    Text {
        id: label
        anchors.centerIn: parent
        width: Math.min(implicitWidth, 460)
        color: Theme.dark ? "#14141f" : "#ffffff"
        font.pixelSize: 13
        elide: Text.ElideRight
    }

    NumberAnimation { id: fadeIn;  target: toast; property: "opacity"; to: 1; duration: 150 }
    NumberAnimation { id: fadeOut; target: toast; property: "opacity"; to: 0; duration: 300 }
    Timer { id: hideTimer; interval: 2400; onTriggered: fadeOut.start() }
}
