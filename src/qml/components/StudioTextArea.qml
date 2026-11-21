import QtQuick
import QtQuick.Controls.Basic as Basic
import BSICustomizer

Basic.TextArea {
    id: control
    implicitHeight: 100
    padding: 12
    color: Theme.text; placeholderTextColor: Theme.textDim
    selectionColor: Theme.accent; selectedTextColor: Theme.primaryText
    font.family: Theme.monoFont; font.pixelSize: 11
    selectByMouse: true; wrapMode: TextEdit.Wrap
    background: Rectangle {
        radius: 10; color: Theme.cardLow
        border.color: control.activeFocus ? Theme.accent : control.hovered ? Theme.hoverBorder : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.motionDuration } }
    }
}
