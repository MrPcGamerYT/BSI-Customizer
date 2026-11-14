import QtQuick
import QtQuick.Controls.Basic
import BSICustomizer

Item {
    id: card
    property string title: "Amethyst"
    property color tint: "#68538e"
    property bool feature: false
    property bool selected: false
    property bool lifted: cardHover.hovered
    signal picked()
    signal played()
    implicitHeight: 176
    Accessible.name: title
    // Hit testing stays on the stationary card, not on the lifted artwork.
    HoverHandler { id: cardHover }
    Rectangle {
        id: face
        anchors.left: parent.left; anchors.right: parent.right
        height: parent.height
        y: card.lifted ? -2 : 0
        radius: 16; color: "transparent"; clip: true
        Behavior on y { NumberAnimation { duration: Theme.motionDuration; easing.type: Easing.OutCubic } }
        Canvas {
            id: artwork; anchors.fill: parent
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                var c=getContext("2d"),w=width,h=height,r=16;
                c.reset(); c.beginPath(); c.moveTo(r,0); c.lineTo(w-r,0); c.quadraticCurveTo(w,0,w,r);
                c.lineTo(w,h-r); c.quadraticCurveTo(w,h,w-r,h); c.lineTo(r,h); c.quadraticCurveTo(0,h,0,h-r);
                c.lineTo(0,r); c.quadraticCurveTo(0,0,r,0); c.closePath(); c.save(); c.clip();
                var g=c.createLinearGradient(0,0,0,h);
                g.addColorStop(0,card.feature ? "#18181f" : Qt.lighter(card.tint,1.09));
                g.addColorStop(1,card.feature ? "#111116" : Qt.darker(card.tint,1.20));
                c.fillStyle=g; c.fillRect(0,0,w,h);
                c.beginPath(); c.moveTo(w*0.88,card.feature ? 0 : h*0.22); c.lineTo(w,card.feature ? 0 : h*0.22);
                c.lineTo(w,h); c.lineTo(w*0.45,h); c.closePath(); c.fillStyle="rgba(8,8,12,0.90)"; c.fill();
                c.strokeStyle="rgba(255,255,255,0.14)"; c.setLineDash([3,4]); c.stroke(); c.restore();
            }
        }
        Connections { target: card; function onTintChanged() { artwork.requestPaint(); } }
        MiniPlayer { width: Math.min(117,parent.width*.44); height: 89; anchors.right: parent.right; anchors.rightMargin: -9; y: card.feature ? 61 : 49; tint: card.feature ? "#767080" : Qt.lighter(card.tint,1.9); rotation: -8 }
        FrameCorners { visible: card.feature; width: 56; height: 99; anchors.right: parent.right; anchors.rightMargin: 11; y: 26; tint: "#45414f" }
        Text {
            x: 20; y: 23; width: parent.width*.70
            visible: card.feature
            text: qsTr("Same dark canvas.\nA different\ncharacter.")
            color: "#f2f2f5"; font.family: Theme.headingFont; font.pixelSize: Math.min(22,parent.width*.078); font.weight: Font.DemiBold; font.letterSpacing: -0.7
        }
        Rectangle {
            visible: card.feature; x: 20; y: parent.height-37; width: 121; height: 20; radius: 10; color: "#f2f2f5"
            MicroLabel { anchors.centerIn: parent; text: qsTr("6 CURATED PACKS"); color: "#15151b"; font.pixelSize: 8; font.letterSpacing: 0.9 }
        }
        Text { visible: !card.feature; x: 20; y: parent.height-45; text: card.title; color: "white"; font.family: Theme.headingFont; font.pixelSize: 23; font.weight: Font.DemiBold; font.letterSpacing: -0.65 }
        Rectangle {
            anchors.fill: parent; radius: 16; color: card.lifted ? Qt.alpha(Theme.accent, 0.07) : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.motionDuration } }
            border.width: 1; border.color: card.selected ? Theme.accent : card.lifted ? Theme.hoverBorder : Theme.border
            Behavior on border.color { ColorAnimation { duration: Theme.motionDuration } }
        }
        AbstractButton {
            id: hit; anchors.fill: parent; Accessible.name: card.title
            background: Item {}
            onClicked: card.picked()
        }
        CircleButton { id: play; visible: !card.feature; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 17; width: 30; height: 30; filled: true; text: qsTr("Preview %1").arg(card.title); onClicked: card.played() }
    }
}
