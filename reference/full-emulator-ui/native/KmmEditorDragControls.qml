
import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15

import UiToolTipControl 1.0
import "KmmUtils.js" as Utils

UiColumnLayout {
    id: palletteItem

    property string componentFile: "KmmEditor" + type + ".qml"
    property string dragImage: "qrcAssets/" + image + ".svg"

    padding: 8
    color: pMouseArea.containsMouse ? UiTheme.colors.primary50 : "transparent"
    border.width: 1
    border.color: UiTheme.colors.primary60

    Rectangle {
        color: "transparent"
        Layout.preferredHeight: 44
        Layout.fillWidth: true
        UiImage {
            asset: image
            pImageWidth: 36
            pImageHeight: 36
            anchors.centerIn: parent
        }
    }
    Text {
        id: iText
        text: label
        Layout.fillWidth: true
        Layout.fillHeight: true
        wrapMode: Text.Wrap
        elide: Text.ElideRight
        horizontalAlignment: Qt.AlignHCenter
        font: UiTheme.fonts.bodyExtraSmall
        color: UiTheme.colors.primary10
    }

    pMouseArea {
        enabled: true
        anchors.fill: palletteItem
        hoverEnabled: true
        cursorShape: Qt.OpenHandCursor
        preventStealing: true
        onPressed: function(mouse) {
            palletteItem.pMouseArea.cursorShape = Qt.ClosedHandCursor
            Utils.startDrag(mouse, type);
        }
        onPositionChanged: function(mouse) {
            Utils.continueDrag(mouse, type);
        }
        onReleased: function(mouse) {
            palletteItem.pMouseArea.cursorShape = Qt.OpenHandCursor
            Utils.endDrag(mouse, type);
        }
    }
}
