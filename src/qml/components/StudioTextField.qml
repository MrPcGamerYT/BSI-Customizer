import QtQuick
import QtQuick.Controls.Basic as Basic
import BSICustomizer

Basic.TextField {
    id: control
    implicitHeight: 36
    leftPadding: 12; rightPadding: 12; topPadding: 8; bottomPadding: 8
    color: Theme.text; placeholderTextColor: Theme.textDim
    selectionColor: Theme.accent; selectedTextColor: Theme.primaryText
    font.family: Theme.bodyFont; font.pixelSize: 12
    selectByMouse: true
    background: Rectangle {
        radius: 10; color: Theme.cardLow
        border.color: control.activeFocus ? Theme.accent : control.hovered ? Theme.hoverBorder : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.motionDuration } }
    }
}
