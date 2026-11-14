import QtQuick
import BSICustomizer

Rectangle {
    id: preview
    property string packName: "VOID / Carbon"
    property color accentColor: "#7c5cff"
    property color surfaceColor: "#101012"
    property int cornerRadius: 4
    property string motion: "fade"
    property int duration: 240
    property string iconStyle: "line"
    property int controlHeight: 32
    implicitHeight: 130
    color: surfaceColor; radius: 6; border.color: "#35333f"
    function replay() {
        entrance.stop(); body.opacity=1;
        if (!Theme.reduceMotion && preview.motion !== "none") entrance.restart();
    }
    // Explicit playback only. Nothing animates on creation/selection or at idle.
    NumberAnimation { id: entrance; target: body; property: "opacity"; from: .15; to: 1; duration: Math.min(400,preview.duration); easing.type: Easing.OutCubic }
    Item {
        id: body; anchors.fill: parent
        Rectangle { x: 1; y: 1; width: parent.width-2; height: 18; radius: 5; color: "#1a1920"
            Text { x: 8; anchors.verticalCenter: parent.verticalCenter; width: parent.width-38; text: preview.packName; color: "#b1acbb"; font.family: Theme.bodyFont; font.pixelSize: 7; elide: Text.ElideRight; textFormat: Text.PlainText }
            Text { anchors.right: parent.right; anchors.rightMargin: 7; anchors.verticalCenter: parent.verticalCenter; text: "−  ×"; color: "#77717f"; font.pixelSize: 8 }
        }
        Rectangle { x: 1; y: 19; width: 21; height: parent.height-20; color: "#14131b"; radius: 3 }
        Column { x: 9; y: 28; spacing: 10
            Repeater { model: 3; Rectangle { width: 5; height: 5; radius: 1; color: Qt.alpha(preview.accentColor,.55) } }
        }
        Text { x: 35; y: 30; width: parent.width-48; text: preview.packName; textFormat: Text.PlainText; color: "#c6c1cf"; font.family: Theme.headingFont; font.pixelSize: 11; font.weight: Font.DemiBold; elide: Text.ElideRight }
        Rectangle { x: 35; y: 49; width: Math.max(1,parent.width-85); height: 2; color: "#302d3a" }
        Rectangle { x: 35; y: parent.height-30; width: 47; height: 14; radius: Math.min(preview.cornerRadius,7); color: Qt.alpha(preview.accentColor,.5) }
    }
}
