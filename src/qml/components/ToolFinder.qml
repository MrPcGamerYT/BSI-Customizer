import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer
import "../ToolCatalog.js" as Catalog

Popup {
    id: finder
    objectName: "toolFinder"
    property bool authorized: false
    readonly property var results: Catalog.search(query.text)
    signal navigate(int route)
    signal settings(int tab)
    parent: Overlay.overlay
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? Math.max(16, (parent.height - height) * 0.3) : 0
    width: Math.min(700, parent ? parent.width - 32 : 700)
    height: Math.min(530, parent ? parent.height - 48 : 530)
    padding: 20
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onAuthorizedChanged: if (!authorized) close()
    onResultsChanged: list.currentIndex = results.length ? 0 : -1
    onOpened: {
        if (!authorized) { close(); return; }
        query.text = "";
        list.currentIndex = results.length ? 0 : -1;
        query.forceActiveFocus();
    }
    function choose(index) {
        if (!authorized || index < 0 || index >= results.length) return;
        const tool = results[index];
        close();
        if (tool.route !== undefined) navigate(tool.route);
        else settings(tool.tab);
    }
    function moveSelection(direction) {
        if (!results.length) return;
        list.currentIndex = Math.max(0, Math.min(results.length - 1, list.currentIndex + direction));
        list.positionViewAtIndex(list.currentIndex, ListView.Contain);
    }
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motionDuration } }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.motionDuration } }
    background: Rectangle { radius: 18; color: Theme.window; border.color: Theme.borderStrong }
    Overlay.modal: Rectangle { color: "#b3000000" }
    contentItem: ColumnLayout {
        spacing: 12
        Label { text: qsTr("Find a tool"); color: Theme.text; font.pixelSize: 20; font.bold: true }
        Label { Layout.fillWidth: true; text: qsTr("Navigate only. Searching never scans files, queues changes or applies patches."); wrapMode: Text.Wrap; color: Theme.textDim }
        StudioTextField {
            id: query; objectName: "toolFinderQuery"
            Layout.fillWidth: true
            placeholderText: qsTr("Try icons, FPS, backups, corners or reduced motion…")
            maximumLength: 160
            Keys.onDownPressed: finder.moveSelection(1)
            Keys.onUpPressed: finder.moveSelection(-1)
            onAccepted: finder.choose(list.currentIndex)
        }
        ListView {
            id: list; objectName: "toolFinderResults"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true; spacing: 5; model: finder.results
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index
                width: list.width; implicitHeight: 68
                onClicked: finder.choose(index)
                Accessible.name: modelData.title
                background: Rectangle {
                    radius: 10
                    color: list.currentIndex === row.index ? Theme.accentSoft : row.hovered ? Theme.hoverFill : "transparent"
                    border.color: list.currentIndex === row.index ? Theme.hoverBorder : "transparent"
                }
                contentItem: ColumnLayout {
                    spacing: 4
                    Label { Layout.fillWidth: true; text: row.modelData.title; textFormat: Text.PlainText; color: Theme.text; elide: Text.ElideRight; font.bold: true }
                    Label { Layout.fillWidth: true; text: row.modelData.detail; textFormat: Text.PlainText; color: Theme.textDim; elide: Text.ElideRight; font.pixelSize: 11 }
                }
            }
            Label { anchors.centerIn: parent; visible: !finder.results.length; text: qsTr("No tools found. Try a shorter phrase."); color: Theme.textDim }
        }
        Label { text: qsTr("↑ ↓ Select     Enter Open     Esc Close     Ctrl+K Find a tool"); color: Theme.tertiary; font.pixelSize: 11 }
    }
}
