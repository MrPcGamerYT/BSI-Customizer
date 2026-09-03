import BSICustomizer
import QtQuick

Rectangle {
    id: button

    property string text
    property bool accent: false

    signal clicked()

    implicitWidth: buttonLabel.implicitWidth + 30
    implicitHeight: 36
    radius: 9
    color: button.accent
           ? (buttonArea.pressed ? Qt.darker(Theme.accent, 1.15) : Theme.accent)
           : ((buttonArea.pressed || buttonArea.containsMouse) ? Theme.raised : "transparent")
    border.width: 1
    border.color: button.accent ? "transparent" : Theme.border
    Behavior on color { ColorAnimation { duration: 120 } }

    Text {
        id: buttonLabel
        anchors.centerIn: parent
        text: button.text
        color: button.accent ? "#ffffff" : Theme.text
        font.pixelSize: 13
        font.weight: Font.Medium
    }

    MouseArea {
        id: buttonArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: button.clicked()
    }
}
