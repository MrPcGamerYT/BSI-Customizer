import QtQuick
import QtQuick.Controls.Basic as Basic
import BSICustomizer

Basic.ComboBox {
    id: control
    implicitHeight: 36; implicitWidth: Math.max(130, implicitContentWidth + 44)
    leftPadding: 12; rightPadding: 30; topPadding: 8; bottomPadding: 8
    opacity: enabled ? 1 : 0.45
    font.family: Theme.bodyFont; font.pixelSize: 12
    contentItem: Text {
        text: control.displayText; textFormat: Text.PlainText
        color: Theme.text; font: control.font
        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
    }
    indicator: Text {
        x: control.width - width - 12; y: (control.height - height) / 2
        text: "⌄"; color: control.hovered ? Theme.accentText : Theme.textDim; font.pixelSize: 16
        rotation: control.popup.visible ? 180 : 0
        Behavior on rotation { NumberAnimation { duration: Theme.motionDuration; easing.type: Easing.OutCubic } }
    }
    background: Rectangle {
        radius: 10; color: Theme.cardLow
        border.color: control.activeFocus ? Theme.accent : control.hovered ? Theme.hoverBorder : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.motionDuration } }
    }
    delegate: Basic.ItemDelegate {
        id: optionRow
        required property int index
        width: control.popup.availableWidth; height: 34
        text: control.textAt(index)
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: optionRow.text; color: Theme.text; font: control.font
            verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight; textFormat: Text.PlainText
        }
        background: Rectangle { radius: 7; color: optionRow.highlighted || optionRow.hovered ? Theme.hoverFill : "transparent" }
    }
    popup: Basic.Popup {
        y: control.height + 5; width: control.width
        implicitHeight: Math.min(280, contentItem.implicitHeight + topPadding + bottomPadding)
        padding: 5; closePolicy: Basic.Popup.CloseOnEscape | Basic.Popup.CloseOnPressOutsideParent
        contentItem: ListView {
            clip: true; implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            Basic.ScrollBar.vertical: Basic.ScrollBar {}
        }
        background: Rectangle { radius: 12; color: Theme.raised; border.color: Theme.borderStrong }
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motionDuration } }
        exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.motionDuration } }
    }
}
