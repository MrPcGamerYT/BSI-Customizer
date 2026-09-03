import BSICustomizer
import QtQuick

Rectangle {
    id: card

    // Children declared inside Card { ... } land in `inner`.
    default property alias cardContent: inner.data
    property real pad: 18

    implicitHeight: inner.childrenRect.height + pad * 2
    color: Theme.card
    radius: 14
    border.width: 1
    border.color: Theme.border

    Item {
        id: inner
        x: card.pad
        y: card.pad
        width: card.width - card.pad * 2
    }
}
