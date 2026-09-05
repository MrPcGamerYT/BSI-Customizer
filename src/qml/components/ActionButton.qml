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
    color: !button.enabled
           ? "transparent"
           : (button.accent
              ? (buttonArea.pressed ? Qt.darker(Theme.accent, 1.15) : Theme.accent)
              : ((buttonArea.pressed || buttonArea.containsMouse) ? Theme.raised : "transparent"))
    border.width: 1
    border.color: button.accent && button.enabled ? "transparent" : Theme.border
    opacity: button.enabled ? 1.0 : 0.45
    Behavior on color { ColorAnimation { duration: 120 } }
    Behavior on opacity { NumberAnimation { duration: 120 } }

    Text {
        id: buttonLabel
        anchors.centerIn: parent
        text: button.text
        color: button.accent && button.enabled ? "#ffffff" : Theme.text
        font.pixelSize: 13
        font.weight: Font.Medium
    }

    MouseArea {
        id: buttonArea
        anchors.fill: parent
        enabled: button.enabled
        hoverEnabled: true
        cursorShape: button.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        // Finish a focused color/path edit BEFORE the action consumes it.
        onPressed: button.forceActiveFocus(Qt.MouseFocusReason)
        onClicked: button.clicked()
    }
}
