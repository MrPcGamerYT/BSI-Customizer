import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Card {
    id: panel
    objectName: "dllStartupPanel"
    signal notify(string message)
    property var review: ({})
    property string reviewPath: ""
    property string reviewName: ""
    property string reviewExport: ""
    property string report: qsTr("Copy your trusted AMD64 DLL beside the selected HD-Player.exe, enter its filename and an exact existing function export, then inspect. Nothing runs or changes on page entry.")
    readonly property bool currentReview: reviewPath === backend.exePath && reviewName === dllName.text && reviewExport === exportName.text && !!review.exeSha256
    readonly property bool ready: currentReview && !!review.canApply && !backend.busy
    function invalidate() {
        review = {}; reviewPath = ""; reviewName = ""; reviewExport = "";
        if (confirmApply) confirmApply.close();
        if (confirmRestore) confirmRestore.close();
    }
    function inspectFiles(announce) {
        const path = backend.exePath;
        const name = dllName.text;
        const symbol = exportName.text;
        const result = backend.dllStartupInspect(name, symbol);
        if (path !== backend.exePath || name !== dllName.text || symbol !== exportName.text) return;
        review = result; reviewPath = path; reviewName = name; reviewExport = symbol;
        report = result.msg || "";
        if (announce) panel.notify(report);
    }
    function applyReviewed() {
        if (!ready || !acknowledge.checked) return;
        const token = review;
        review = {};
        const result = backend.dllStartupApply(dllName.text, token.exeSha256, token.dllSha256, true, exportName.text);
        inspectFiles(false); report = result.msg || ""; panel.notify(report);
    }
    function restoreReviewed() {
        if (reviewPath !== backend.exePath || !review.canRestore || backend.busy) return;
        review = {};
        const result = backend.dllStartupRestore();
        inspectFiles(false); report = result.msg || ""; panel.notify(report);
    }
    Connections {
        target: backend
        function onExePathChanged() { panel.invalidate(); }
        function onWorkspaceChanged() { panel.invalidate(); }
    }
    ColumnLayout {
        width: parent.width
        spacing: 12
        MicroLabel { text: qsTr("NATIVE TOOL / NORMAL STARTUP DLL") }
        Label { Layout.fillWidth: true; text: qsTr("Your DLL. Every normal launch."); font.pixelSize: 22; font.bold: true; color: Theme.text; wrapMode: Text.Wrap }
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textDim
            text: qsTr("Adds a direct PE startup dependency, not a version.dll proxy, injector or recurring launcher. After patching, recipients need the EXE WITH the named DLL and its dependencies in a compatible emulator installation. They do not need Python, Node or BSI to launch it.")
        }
        Label { Layout.fillWidth: true; text: backend.exePath || qsTr("Select HD-Player.exe on Home first."); textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; color: Theme.textDim; font.family: Theme.monoFont; font.pixelSize: 11 }
        Label { text: qsTr("DLL filename (already beside the EXE)"); color: Theme.text }
        StudioTextField {
            id: dllName; objectName: "startupDllName"
            Layout.fillWidth: true; text: "zlag.dll"; maximumLength: 120; selectByMouse: true
            Accessible.name: qsTr("Startup DLL filename")
            onTextChanged: panel.invalidate()
        }
        Label { text: qsTr("Existing native function export (resolved, NEVER called)"); color: Theme.text }
        StudioTextField {
            id: exportName; objectName: "startupDllExport"
            Layout.fillWidth: true; text: "zlag_init"; maximumLength: 255; selectByMouse: true
            Accessible.name: qsTr("Exact startup DLL export name")
            onTextChanged: panel.invalidate()
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.warn
            text: qsTr("Close the emulator first. Patching invalidates the EXE signature. A missing, incompatible or failing DLL can stop Windows from launching it. The exact, case-sensitive export must exist or inspection is refused. Normal DLL initialization runs; NO exported function (including zlag_init) is called. Use only a trusted DLL designed for startup initialization.")
        }
        Flow {
            Layout.fillWidth: true; Layout.preferredHeight: childrenRect.height; spacing: 8
            ActionButton { objectName: "inspectStartupDll"; text: qsTr("1. Inspect EXE + DLL"); enabled: !backend.busy && backend.exePath.length > 0; onClicked: panel.inspectFiles(true) }
            ActionButton { objectName: "applyStartupDll"; text: qsTr("2. Review & add import"); accent: true; enabled: panel.ready; onClicked: confirmApply.open() }
            ActionButton { objectName: "restoreStartupDll"; text: qsTr("Restore original EXE"); enabled: !backend.busy && panel.reviewPath === backend.exePath && !!panel.review.canRestore; onClicked: confirmRestore.open() }
        }
        Label { objectName: "startupDllReport"; Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText; text: panel.report; color: Theme.textDim }
        StudioTextArea {
            Layout.fillWidth: true; Layout.preferredHeight: visible ? 110 : 0
            visible: panel.currentReview; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
            font.family: Theme.monoFont; font.pixelSize: 10; color: Theme.textDim
            text: panel.currentReview ? "EXE SHA-256: " + panel.review.exeSha256 + "\nDLL SHA-256: " + panel.review.dllSha256
                + "\nDLL direct imports (not a full dependency check): " + (panel.review.dllDirectImports || []).join(", ") : ""
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.tertiary; font.pixelSize: 11
            text: qsTr("Only the EXE is edited. Unique original backups are never overwritten. Restore refuses unrelated later edits or updates. This is separate from GIF RPC; no third-party patcher or DLL is bundled, and inspecting/patching does not certify your DLL's behavior.")
        }
    }
    Dialog {
        id: confirmApply; objectName: "startupDllConfirm"
        parent: Overlay.overlay; anchors.centerIn: parent; modal: true
        width: Math.min(620, parent ? parent.width - 32 : 620)
        height: Math.min(440, parent ? parent.height - 32 : 440)
        title: qsTr("Add this DLL as a required startup dependency?")
        onOpened: acknowledge.checked = false
        contentItem: ScrollView {
            clip: true; contentWidth: availableWidth
            ColumnLayout {
                width: parent.width; spacing: 12
                Label { Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText; color: Theme.text; text: panel.review.warning || "" }
                Label { Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: Theme.textDim; text: panel.reviewPath + "\n" + panel.reviewName + "!" + panel.reviewExport }
                StudioCheckBox { id: acknowledge; objectName: "startupDllAcknowledge"; Layout.fillWidth: true; text: qsTr("I am authorized, trust this DLL and accept these risks.") }
            }
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            ActionButton { text: qsTr("Cancel"); onClicked: confirmApply.reject() }
            ActionButton { objectName: "confirmStartupDllApply"; text: qsTr("Add startup import"); accent: true; enabled: acknowledge.checked && panel.ready; onClicked: { panel.applyReviewed(); confirmApply.close(); } }
        }
    }
    Dialog {
        id: confirmRestore; parent: Overlay.overlay; anchors.centerIn: parent; modal: true
        width: Math.min(580, parent ? parent.width - 32 : 580)
        height: Math.min(300, parent ? parent.height - 32 : 300)
        title: qsTr("Restore the pre-import EXE?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { wrapMode: Text.Wrap; color: Theme.text; text: qsTr("Close every player instance first. The exact original snapshot will be restored only if this EXE still matches the recorded transaction. Later edits/updates are not discarded. The DLL and unique backup are kept.") }
        onAccepted: panel.restoreReviewed()
    }
}
