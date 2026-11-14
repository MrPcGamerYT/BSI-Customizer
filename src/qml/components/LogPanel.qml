import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer
import "../Icons.js" as Icons
Rectangle {
    id: panel
    property bool collapsed: true
    property string latest: qsTr("Workspace ready. No changes applied.")
    color: Theme.card; radius: 16; border.color: Theme.border
    function append(msg, colorName) {
        latest=msg; logModel.append({msg:msg,col:colorName || "gray"});
        if(logModel.count>400) logModel.remove(0);
        if(!collapsed) Qt.callLater(function() { view.positionViewAtEnd(); });
    }
    ListModel { id: logModel }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 18; spacing: 10
        RowLayout {
            Layout.fillWidth: true; spacing: 9
            Image { source: Icons.icon("<path d='M8 6h13M8 12h13M8 18h13M3 6h1M3 12h1M3 18h1'/>",Theme.tertiary); sourceSize: Qt.size(13,13); Layout.preferredWidth: 13; Layout.preferredHeight: 13 }
            MicroLabel { text: qsTr("RECENT ACTIVITY"); font.pixelSize: 9; color: Theme.tertiary; Layout.fillWidth: true }
            AbstractButton {
                id: toggle; implicitWidth: 20; implicitHeight: 16; text: panel.collapsed ? "⌃" : "⌄"
                Accessible.name: panel.collapsed ? qsTr("Expand activity log") : qsTr("Collapse activity log")
                contentItem: Text { text: toggle.text; color: Theme.textDim; horizontalAlignment: Text.AlignHCenter }
                onClicked: panel.collapsed=!panel.collapsed
            }
        }
        Text { visible: panel.collapsed; Layout.fillWidth: true; text: panel.latest; textFormat: Text.PlainText; color: Theme.textDim; font.family: Theme.bodyFont; font.pixelSize: 11; elide: Text.ElideRight }
        ListView {
            id: view; visible: !panel.collapsed; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            model: logModel; boundsBehavior: Flickable.StopAtBounds; ScrollBar.vertical: ScrollBar {}
            delegate: Text { required property string msg; required property string col; width: ListView.view.width; text: msg; textFormat: Text.PlainText; color: Theme.logColor(col); font.family: Theme.monoFont; font.pixelSize: 10; wrapMode: Text.Wrap }
        }
    }
}
