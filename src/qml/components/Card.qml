import BSICustomizer
import QtQuick

Rectangle {
    id: card

    // Children declared inside Card { ... } land in `inner`.
    default property alias cardContent: inner.data
    property real pad: 18

    // Two height rules, and they must not feed each other:
    //  * a page that sets Layout.preferredHeight / Layout.fillHeight wins, and
    //    `inner` hands that height to children that use anchors.fill: parent
    //    (the pattern every page uses) - without it those children collapsed to
    //    zero height and the card rendered as an empty box;
    //  * otherwise the card wraps its content, measured from the children's
    //    implicitHeight.  childrenRect is deliberately NOT used here: it would
    //    depend on inner.height, which depends on card.height, which depends on
    //    implicitHeight - a binding loop.
    implicitHeight: {
        let h = 0;
        for (let i = 0; i < inner.children.length; ++i) {
            const c = inner.children[i].implicitHeight;
            // non-visual children (Timer, Connections, ...) have no implicit
            // size; feeding undefined to Math.max would poison it with NaN
            if (c > 0)
                h = c;
        }
        return h + pad * 2;
    }
    implicitWidth: {
        let w = 0;
        for (let i = 0; i < inner.children.length; ++i) {
            const c = inner.children[i].implicitWidth;
            if (c > 0)
                w = c;
        }
        return w + pad * 2;
    }

    color: Theme.card
    radius: 14
    border.width: 1
    border.color: Theme.border

    Item {
        id: inner
        x: card.pad
        y: card.pad
        width: card.width - card.pad * 2
        height: card.height - card.pad * 2
    }
}
