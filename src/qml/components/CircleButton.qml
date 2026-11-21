import QtQuick
import QtQuick.Controls.Basic
import BSICustomizer
import "../Icons.js" as Icons
AbstractButton {
    id: control
    property string glyph: "<path d='m9 6 9 6-9 6z'/>"
    property bool filled: false
    implicitWidth: 32; implicitHeight: 32
    Accessible.name: text
    ToolTip.visible: hovered
    ToolTip.text: text
    background: Rectangle {
        radius: width / 2
        color: control.filled ? (control.down ? "#cfcfd9" : "#f2f2f5") : control.hovered || control.down ? Theme.hoverFill : "transparent"
        border.color: control.activeFocus ? Theme.accent : control.filled ? "transparent" : control.hovered ? Theme.hoverBorder : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.motionDuration } }
        Behavior on color { ColorAnimation { duration: Theme.motionDuration } }
    }
    contentItem: Image {
        source: Icons.icon(control.glyph, control.filled ? "#101015" : Theme.textDim)
        sourceSize: Qt.size(14,14); fillMode: Image.Pad
    }
}
