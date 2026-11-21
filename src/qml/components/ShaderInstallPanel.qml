import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Card {
    id: panel
    objectName: "shaderInstallPanel"
    signal notify(string message)
    signal prepareSettingsPanel()
    property var review: ({})
    property string reviewPath: ""
    property string operation: "install"
    property string report: qsTr("Nothing has been changed. Inspect the selected player to check native source capacity, the exact Qt release version and the bundled regular shader module.")
    readonly property bool currentReview: reviewPath === backend.exePath && !!review.exeSha256
    readonly property bool ready: currentReview && !backend.busy
    function invalidate() {
        review = ({}); reviewPath = "";
        report = qsTr("The previous review is no longer current. Inspect the selected player again before making changes.");
        if (confirmation) confirmation.close();
    }
    function inspectFiles(announce) {
        if (backend.busy) return;
        const path = backend.exePath;
        const response = backend.shadersInspect();
        if (path !== backend.exePath) return;
        review = response; reviewPath = path; report = response.msg || "";
        if (announce) panel.notify(report);
    }
    function canRun(kind) {
        return ready && (kind === "install" ? !!review.canApply
                        : kind === "remove" ? !!review.canRemove : !!review.canRecover);
    }
    function ask(kind) {
        if (!canRun(kind)) return;
        operation = kind; confirmation.open();
    }
    function runReviewed() {
        if (!canRun(operation) || !acknowledge.checked) return;
        const token = review;
        const path = reviewPath;
        const kind = operation;
        review = ({}); reviewPath = "";
        confirmation.close();
        let response;
        if (kind === "install") response = backend.shadersApply(token.bundleId, token.exeSha256, token.packageSha256, token.applyReviewSha256, true);
        else if (kind === "remove") response = backend.shadersRemove(token.exeSha256, token.stateSha256, true);
        else response = backend.shadersRecover(token.journalSha256, true);
        if (path === backend.exePath) {
            inspectFiles(false);
            report = (response.msg || "") + "\n\n" + report;
        }
        panel.notify(response.msg || "");
    }
    Connections {
        target: backend
        function onExePathChanged() { panel.invalidate(); }
        function onWorkspaceChanged() { panel.invalidate(); }
        function onResourcesChanged() { panel.invalidate(); }
    }
    ColumnLayout {
        width: parent.width
        spacing: 12
        MicroLabel { text: qsTr("EXPERIMENTAL / NATIVE PLAYER SETTINGS") }
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap
            text: qsTr("Shaders in the player's own Settings")
            font.pixelSize: 22; font.bold: true; color: Theme.text
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textDim
            text: qsTr("Installs the bundled regular BSIShaders.dll QML module and a genuine Settings → Shaders tab. Not a version.dll proxy, overlay or customizer-only preview. Only shader controls are added; no FPS, ADB, registry, optimizer or other system changes.")
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText
            text: backend.exePath || qsTr("Select HD-Player.exe on Home first.")
            color: Theme.textDim; font.family: Theme.monoFont; font.pixelSize: 11
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.warn
            text: qsTr("Before testing: close all player instances and keep a separate full installation backup. The EXE signature is invalidated. A DLL, graphics hook or driver problem can crash the player; real Intel/AMD/NVIDIA and installed-player verification is still required. Unsupported or uncertain game surfaces are bypassed, not forced.")
        }
        Flow {
            Layout.fillWidth: true; Layout.preferredHeight: childrenRect.height; spacing: 8
            ActionButton {
                objectName: "inspectShaders"; text: qsTr("1. Inspect compatibility")
                enabled: !backend.busy && !!backend.exePath
                onClicked: panel.inspectFiles(true)
            }
            ActionButton {
                objectName: "installShaders"
                text: panel.review.updateAvailable ? qsTr("2. Review update") : panel.review.installed ? qsTr("2. Review reapply") : qsTr("2. Review installation")
                accent: true; enabled: panel.canRun("install")
                onClicked: panel.ask("install")
            }
            ActionButton {
                objectName: "removeShaders"; text: qsTr("Remove shaders only")
                enabled: panel.canRun("remove"); onClicked: panel.ask("remove")
            }
            ActionButton {
                objectName: "recoverShaders"; text: qsTr("Recover transaction")
                visible: !!panel.review.recoveryRequired
                enabled: panel.canRun("recover"); onClicked: panel.ask("recover")
            }
        }
        Label {
            objectName: "shaderInstallReport"
            Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
            text: panel.report; color: panel.review.recoveryRequired ? Theme.warn : Theme.textDim
        }
        StudioTextArea {
            objectName: "shaderInspectionHashes"
            Layout.fillWidth: true; Layout.preferredHeight: visible ? 115 : 0
            visible: panel.currentReview; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
            font.family: Theme.monoFont; font.pixelSize: 10; color: Theme.textDim
            text: !panel.currentReview ? "" : "EXE SHA-256: " + panel.review.exeSha256
                + "\nBundle: " + (panel.review.bundleId || "none") + "\nPackage SHA-256: " + (panel.review.packageSha256 || "none")
                + "\nInstalled package SHA-256: " + (panel.review.installedPackageSha256 || "none")
                + (panel.review.journalSha256 ? "\nRecovery journal SHA-256: " + panel.review.journalSha256 : "")
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.text; font.bold: true
            text: qsTr("Where to find it after installation")
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textDim
            text: qsTr("1. The native top-tabs Settings redesign must already be applied. If inspection refuses the native shell, prepare it in Settings panel, then inspect here again.\n2. Launch the patched player normally. Open its sidebar Settings and choose Shaders; use the top strip's scrolling if necessary.\n3. Select Vivid or Clear, enable effects and Save changes. Watch the actual game image, observed game API and processed-frame counter. Saved sliders or a loaded DLL alone are not proof of working effects.\n4. Test OpenGL, DirectX and Vulkan separately using the game's Graphics renderer setting and restart the player after each renderer change. Disable effects to compare, then remove shaders here if needed.")
        }
        ActionButton {
            objectName: "shaderPreparePanel"; text: qsTr("Open Settings panel preparation")
            enabled: !backend.busy; onClicked: panel.prepareSettingsPanel()
        }
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.tertiary; font.pixelSize: 11
            text: qsTr("Installation refuses an incompatible Qt ABI/version or dependency set, unknown/edited native sources or unowned files. No runtime DLL is replaced. Removal deactivates the native shader source first and keeps the Settings redesign, unrelated edits and saved preferences. Interrupted transactions retain verified snapshots; never delete their journal/recovery folder or launch the player until recovery succeeds. Two filesystem objects are recoverable, not simultaneously atomic or power-loss certified.")
        }
    }
    Dialog {
        id: confirmation; objectName: "shaderInstallConfirm"
        parent: Overlay.overlay; anchors.centerIn: parent; modal: true
        width: Math.min(650, parent ? parent.width - 32 : 650)
        height: Math.min(490, parent ? parent.height - 32 : 490)
        title: panel.operation === "install" ? qsTr("Install this experimental native shader module?")
               : panel.operation === "remove" ? qsTr("Remove only the owned shader integration?") : qsTr("Recover this interrupted shader transaction?")
        onOpened: acknowledge.checked = false
        contentItem: ScrollView {
            clip: true; contentWidth: availableWidth
            ColumnLayout {
                width: parent.width; spacing: 12
                Label {
                    Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; color: Theme.text
                    text: panel.reviewPath + (panel.operation === "install" ? "\n" + (panel.review.bundleId || "")
                        + "\nPackage SHA-256: " + (panel.review.packageSha256 || "") : "")
                }
                Label {
                    Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.warn
                    text: panel.operation === "install"
                        ? qsTr("I have closed every player instance, backed up the installation and accept invalidating the EXE signature and loading this bundled regular DLL. Compatibility checks do not certify game rendering, GPU/driver safety or anti-cheat acceptance. I will test a disposable installation first.")
                        : panel.operation === "remove"
                          ? qsTr("Close every player instance. This removes the exact owned source fragments and four-file shader module, not the native Settings redesign or saved preferences. Edited, conflicting or unowned files are refused rather than overwritten.")
                          : qsTr("Close every player instance and keep all recovery files. A prepared transaction is rolled back to its verified previous pair; a committed transaction is only verified and cleaned up. Any external conflict stops recovery without overwriting it.")
                }
                StudioCheckBox {
                    id: acknowledge; objectName: "shaderInstallAcknowledge"
                    Layout.fillWidth: true
                    text: qsTr("I am authorized, have a backup and accept this experimental operation.")
                }
            }
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            ActionButton { text: qsTr("Cancel"); onClicked: confirmation.reject() }
            ActionButton {
                objectName: "confirmShaderOperation"
                text: panel.operation === "install" ? qsTr("Install native shaders") : panel.operation === "remove" ? qsTr("Remove shaders") : qsTr("Recover verified transaction")
                accent: true; enabled: acknowledge.checked && panel.canRun(panel.operation)
                onClicked: panel.runReviewed()
            }
        }
    }
}
