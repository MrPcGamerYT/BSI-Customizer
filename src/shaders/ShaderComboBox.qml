import QtQuick 2.15
import QtQuick.Controls 2.15

ComboBox {
    id: control
    property var shaderStyle
    implicitWidth: 200; implicitHeight: shaderStyle.comboHeight
    leftPadding: 8; rightPadding: 32
    font: shaderStyle.bodyFont
    hoverEnabled: true
    contentItem: Text {
        text: control.displayText; font: control.font
        color: control.enabled ? control.shaderStyle.secondary : control.shaderStyle.muted
        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
    }
    indicator: Text {
        x: control.width - 24; y: (control.height - height) / 2
        text: "▾"; color: control.shaderStyle.secondary; font.pixelSize: 12
    }
    background: ShaderSurface {
        base: control.shaderStyle.field
        border.color: control.activeFocus || control.hovered ? control.shaderStyle.accent : control.shaderStyle.border
        border.width: 1
    }
    delegate: ItemDelegate {
        width: control.width; height: control.shaderStyle.comboHeight
        text: modelData; font: control.font
        highlighted: control.highlightedIndex === index
        hoverEnabled: true
        contentItem: Text {
            text: parent.text; font: parent.font; leftPadding: 8
            color: control.shaderStyle.secondary; verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: parent.highlighted || parent.hovered ? control.shaderStyle.separator : control.currentIndex === index ? control.shaderStyle.panel : "transparent"
        }
    }
    popup: Popup {
        y: control.height; width: control.width; padding: 1
        height: Math.min(contentItem.implicitHeight + 2, 162)
        contentItem: ListView {
            clip: true; implicitHeight: contentHeight
            model: control.delegateModel; currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            ScrollBar.vertical: ShaderScrollBar { shaderStyle: control.shaderStyle }
        }
        background: ShaderSurface {
            base: control.shaderStyle.field
            border.color: control.shaderStyle.border
        }
    }
}
