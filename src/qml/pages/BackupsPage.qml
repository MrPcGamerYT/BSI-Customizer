import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer
import "../ToolCatalog.js" as Catalog

Item {
    id: page
    signal notify(string message)
    property var entries: []
    property bool loaded: false
    property string report: qsTr("Refresh explicitly to load the latest 200 workspace journals. Opening this page does not read snapshots.")
    property var pendingRestore: ({})
    property string restoreTarget: ""
    readonly property var filteredEntries: Catalog.history(entries, query.text, targetOnly.checked ? backend.exePath : "")
    readonly property var selectedEntry: filteredEntries[selection.currentIndex] || ({})
    onFilteredEntriesChanged: selection.currentIndex = filteredEntries.length ? 0 : -1
    function refresh() {
        const r = backend.workspace("history", {});
        entries = r.ok ? (r.entries || []) : [];
        loaded = !!r.ok;
        report = r.msg || "";
        page.notify(report);
    }
    function reviewRestore() {
        if (!selectedEntry.id || !backend.exePath || backend.busy) return;
        pendingRestore = JSON.parse(JSON.stringify(selectedEntry));
        restoreTarget = backend.exePath;
        confirmRestore.open();
    }
    Connections {
        target: backend
        function onExePathChanged() {
            page.entries = []; page.loaded = false; page.pendingRestore = ({});
            page.report = qsTr("Target changed. Refresh snapshots before selecting a restore point.");
            confirmRestore.close();
        }
    }
    ScrollView {
        anchors.fill: parent; clip: true; contentWidth: availableWidth
        ColumnLayout {
            width: parent.width; spacing: 14
            Label { text: qsTr("Backups & recovery"); color: Theme.text; font.pixelSize: 26; font.bold: true }
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textDim; text: qsTr("Workspace and Add-ons preserve transactional snapshots. A listed journal is not proof that restore is currently possible: the engine verifies the target path, current EXE and snapshot hashes at restore time. Legacy .bak files stay on their original editor pages.") }
            RowLayout {
                Layout.fillWidth: true
                ActionButton { objectName: "refreshRecovery"; text: qsTr("Refresh snapshots"); enabled: !backend.busy; onClicked: page.refresh() }
                Label { Layout.fillWidth: true; text: page.filteredEntries.length + qsTr(" shown / ") + page.entries.length + qsTr(" loaded"); color: Theme.textDim }
            }
            StudioTextField { id: query; objectName: "recoverySearch"; Layout.fillWidth: true; maximumLength: 160; placeholderText: qsTr("Filter by target, date, state or snapshot ID…") }
            StudioCheckBox { id: targetOnly; Layout.fillWidth: true; text: qsTr("Only this target (textual path match; turn off for path aliases)"); checked: true }
            Label { Layout.fillWidth: true; text: qsTr("Target: ") + (backend.exePath || qsTr("None selected — choose one on Overview")); textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; color: Theme.textDim }
            ListView {
                id: selection; objectName: "recoverySnapshots"
                Layout.fillWidth: true; Layout.preferredHeight: 240
                model: page.filteredEntries; spacing: 6; clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    id: snapshot
                    required property var modelData
                    required property int index
                    width: selection.width; implicitHeight: 88
                    onClicked: selection.currentIndex = index
                    background: Rectangle {
                        radius: 12; color: selection.currentIndex === snapshot.index ? Theme.accentSoft : Theme.card
                        border.color: selection.currentIndex === snapshot.index ? Theme.hoverBorder : Theme.border
                    }
                    contentItem: ColumnLayout {
                        spacing: 4
                        Label { Layout.fillWidth: true; text: Catalog.filename(snapshot.modelData.path) + " · " + Catalog.stateLabel(snapshot.modelData.state); textFormat: Text.PlainText; color: Theme.text; elide: Text.ElideRight; font.bold: true }
                        Label { Layout.fillWidth: true; text: (snapshot.modelData.time || qsTr("Time not recorded")) + " · " + (snapshot.modelData.operationCount || 0) + qsTr(" operation(s)"); textFormat: Text.PlainText; color: Theme.textDim; elide: Text.ElideRight }
                        Label { Layout.fillWidth: true; text: snapshot.modelData.path || ""; textFormat: Text.PlainText; color: Theme.tertiary; elide: Text.ElideMiddle; font.pixelSize: 11 }
                    }
                }
                Label {
                    anchors.centerIn: parent; width: parent.width - 24; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
                    visible: !page.filteredEntries.length; color: Theme.textDim
                    text: !page.loaded ? qsTr("No history loaded. Use Refresh snapshots.") : qsTr("No matching journals. Clear the filter or show all targets. Legacy backups are not included in this list.")
                }
            }
            Rectangle {
                Layout.fillWidth: true; implicitHeight: details.implicitHeight + 28
                radius: 12; color: Theme.card; border.color: Theme.border
                ColumnLayout {
                    id: details
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14; spacing: 9
                    Label { text: qsTr("Selected snapshot"); color: Theme.text; font.bold: true }
                    Label { Layout.fillWidth: true; text: page.selectedEntry.id || qsTr("Select a snapshot above."); textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; color: Theme.textDim }
                    Label { Layout.fillWidth: true; text: qsTr("Before: ") + (page.selectedEntry.before || "—"); textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; color: Theme.textDim; font.family: Theme.monoFont; font.pixelSize: 11 }
                    Label { Layout.fillWidth: true; text: qsTr("After: ") + (page.selectedEntry.after || "—"); textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; color: Theme.textDim; font.family: Theme.monoFont; font.pixelSize: 11 }
                    Repeater {
                        model: page.selectedEntry.operations || []
                        Label {
                            required property var modelData
                            Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; color: Theme.textDim; textFormat: Text.PlainText
                            text: "• " + (modelData.name || modelData.path || modelData.kind || qsTr("Operation"))
                        }
                    }
                    StudioCheckBox { id: showMetadata; text: qsTr("Show technical metadata"); checked: false }
                    StudioTextArea { visible: showMetadata.checked; Layout.fillWidth: true; Layout.preferredHeight: 140; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; text: JSON.stringify(page.selectedEntry, null, 2); color: Theme.textDim }
                }
            }
            ActionButton { objectName: "reviewRecovery"; text: qsTr("Review restore…"); enabled: !!page.selectedEntry.id && !!backend.exePath && !backend.busy; onClicked: page.reviewRestore() }
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textDim; textFormat: Text.PlainText; text: page.report }
            Item { Layout.preferredHeight: 20 }
        }
    }
    Dialog {
        id: confirmRestore; objectName: "recoveryConfirmDialog"
        anchors.centerIn: parent; width: Math.min(560, page.width - 24); modal: true
        title: qsTr("Restore this exact snapshot?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label {
            wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: Theme.text
            text: qsTr("Selected target: ") + page.restoreTarget + qsTr("\n\nSnapshot: ") + (page.pendingRestore.id || "")
                + qsTr("\nRecorded target: ") + (page.pendingRestore.path || "")
                + qsTr("\n\nClose all player instances. The engine will refuse a different target, missing/corrupt snapshot, or a current EXE that no longer matches the recorded post-patch hash. No automatic force-restore is offered.")
        }
        onAccepted: {
            if (backend.busy || backend.exePath !== page.restoreTarget || !page.pendingRestore.id) return;
            const r = backend.workspace("restore", {id: page.pendingRestore.id});
            page.report = r.msg || ""; page.notify(page.report);
            if (r.ok) { page.entries = []; page.loaded = false; }
            page.pendingRestore = ({});
        }
        onRejected: page.pendingRestore = ({})
    }
}
