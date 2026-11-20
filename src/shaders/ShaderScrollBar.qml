import QtQuick 2.15
import QtQuick.Controls 2.15

ScrollBar {
    id: control
    property var shaderStyle
    implicitWidth: 6; implicitHeight: 6
    padding: 0
    minimumSize: .08
    policy: ScrollBar.AsNeeded
    contentItem: Rectangle {
        implicitWidth: 6; implicitHeight: 6
        // Never grade/alpha-mask the thumb. It must retain its native color.
        color: control.hovered || control.pressed ? control.shaderStyle.scrollHover : control.shaderStyle.scrollColor
    }
    background: null
}
