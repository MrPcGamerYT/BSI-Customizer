import QtQuick 2.15
import QtQuick.Controls 2.15

Slider {
    id: control
    property var shaderStyle
    implicitWidth: 200; implicitHeight: 28
    padding: 0
    handle: ShaderSurface {
        x: control.visualPosition * (control.width - width)
        y: (control.height - height) / 2
        implicitWidth: control.shaderStyle.sliderHandle
        implicitHeight: control.shaderStyle.sliderHandle
        radius: width / 2
        base: control.enabled ? control.shaderStyle.sliderHandleColor : control.shaderStyle.muted
        border.width: 2; border.color: control.shaderStyle.sliderHandleBorder
    }
    background: Rectangle {
        x: control.handle.width / 2; y: (control.height - height) / 2
        width: control.width - control.handle.width; height: 2
        color: control.enabled ? control.shaderStyle.sliderTrack : control.shaderStyle.sliderDisabled
        Rectangle {
            width: control.visualPosition * parent.width; height: parent.height
            color: control.enabled ? control.shaderStyle.sliderFill : control.shaderStyle.sliderDisabled
        }
    }
}
