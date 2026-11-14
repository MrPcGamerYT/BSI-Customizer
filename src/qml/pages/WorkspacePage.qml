import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia
import BSICustomizer
import "../ToolCatalog.js" as Catalog

Item {
    id: page
    signal navigate(int index)
    property string report: qsTr("Inspect explicitly, stage changes, Dry Run, then confirm Apply. Opening this page never scans resources.")
    property var inspection: ({})
    property var plan: ({})
    property var historyRows: []
    property string comparisonImage: ""
    property string comparisonText: ""
    property string originalImage: ""
    property string originalText: ""
    property string imageSource: ""
    property url imageSourceUrl: ""
    property string preparedImage: ""
    property url videoUrl: ""
    property bool brandingOnly: false
    readonly property var assets: {
        const rows = backend.resourceInfos;
        return brandingOnly ? rows.filter(r => /logo|splash|brand|icon|banner/i.test(r.path)) : rows;
    }
    function show(r) { report = r.msg || ""; return r; }
    function run(action, options) { return show(backend.workspace(action, options || {})); }
    Connections {
        target: backend
        function onWorkspaceChanged() { page.plan = ({}); applyDialog.close(); }
        function onExePathChanged() { page.plan = ({}); applyDialog.close(); restoreDialog.close(); page.inspection = ({}); page.historyRows = []; page.originalImage = ""; page.comparisonImage = ""; }
    }
    MediaPlayer {
        id: videoPlayer
        source: page.videoUrl
        videoOutput: videoOutput
        audioOutput: AudioOutput { muted: true }
        loops: MediaPlayer.Infinite
        onErrorOccurred: page.report = qsTr("Customizer playback error: ") + errorString
    }
    onVisibleChanged: if (!visible) videoPlayer.pause()
    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: parent.width
            spacing: 14
            Text { text: qsTr("Customization Workspace"); color: Theme.text; font.pixelSize: 24; font.bold: true; Layout.margins: 24 }
            Label { Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; text: qsTr("Verified storage • portable profiles • isolated dry runs • recoverable single-EXE batches"); color: Theme.textDim; wrapMode: Text.Wrap }
            PathRow { Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; label: "HD-Player.exe"; text: backend.exePath; onLoad: backend.exePath = text }
            Card {
                Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24
                ColumnLayout {
                    width: parent.width; spacing: 10
                    Label { text: qsTr("Compatibility / capacity inspector"); color: Theme.text; font.bold: true }
                    RowLayout {
                        ActionButton { text: qsTr("Inspect selected EXE"); enabled: !backend.busy; onClicked: page.inspection = page.run("inspect") }
                        ActionButton { text: qsTr("Export diagnostics…"); enabled: !backend.busy; onClicked: reportDialog.open() }
                    }
                    Label { Layout.fillWidth: true; color: page.inspection.themeSupported ? Theme.accentText : Theme.textDim; wrapMode: Text.Wrap; text: page.inspection.msg || qsTr("Inspection is manual. Static mapping verification does not prove runtime compatibility.") }
                    StudioTextArea { Layout.fillWidth: true; readOnly: true; selectByMouse: true; color: Theme.textDim; wrapMode: TextEdit.Wrap; text: page.inspection.sha256 ? "SHA-256: " + page.inspection.sha256 + "\nQt imports: " + (page.inspection.qtMajor || "unknown") + " · encoded size: " + page.inspection.required + " · capacity: " + page.inspection.capacity + "\nDecoded original / replacement: " + page.inspection.originalDecodedBytes + " / " + page.inspection.replacementDecodedBytes + " · compressed original / replacement: " + page.inspection.originalCompressedBytes + " / " + page.inspection.replacementCompressedBytes + "\nRelocation: " + page.inspection.relocationSupport + "\n" + (page.inspection.mapping || "") : "" }
                }
            }
            Card {
                Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24
                ColumnLayout {
                    width: parent.width; spacing: 10
                    Label { text: qsTr("Profiles: Theme + Boot + About + GIF RPC + Appearance"); color: Theme.text; font.bold: true }
                    Label { Layout.fillWidth: true; text: qsTr("Stage settings using the Save to profile buttons on each editor. Import never deploys files. Load the target theme first to match colors; unmatched properties are reported."); color: Theme.textDim; wrapMode: Text.Wrap }
                    RowLayout {
                        StudioTextField { id: profileName; Layout.fillWidth: true; text: backend.customizationProfile.name || "My workspace"; placeholderText: qsTr("Profile name") }
                        ActionButton { text: qsTr("Import…"); onClicked: loadProfileDialog.open() }
                        ActionButton { text: qsTr("Export…"); onClicked: saveProfileDialog.open() }
                    }
                    StudioTextArea { Layout.fillWidth: true; Layout.preferredHeight: 130; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; color: Theme.textDim; text: JSON.stringify(backend.customizationProfile, null, 2) }
                }
            }
            Card {
                Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24
                ColumnLayout {
                    width: parent.width; spacing: 10
                    Label { text: qsTr("Branding assets / original vs replacement"); color: Theme.text; font.bold: true }
                    Label { Layout.fillWidth: true; color: Theme.textDim; wrapMode: Text.Wrap; text: qsTr("Use Scan Resources on the Resources page first. Select a supported logo, splash, image or text resource below. Unknown mappings are never force-written.") }
                    RowLayout {
                        ActionButton { text: qsTr("Resources →"); onClicked: page.navigate(2) }
                        ActionButton { text: qsTr("About text / links →"); onClicked: page.navigate(6) }
                        StudioCheckBox { text: qsTr("Branding names only"); checked: page.brandingOnly; onToggled: page.brandingOnly = checked }
                    }
                    StudioComboBox { id: resourceChoice; Layout.fillWidth: true; model: page.assets; textRole: "path" }
                    ActionButton {
                        text: qsTr("Choose replacement & queue…"); enabled: page.assets.length > 0 && !backend.busy
                        onClicked: replacementDialog.open()
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Original"); color: Theme.textDim }
                            Image { Layout.fillWidth: true; Layout.preferredHeight: 150; source: page.originalImage; fillMode: Image.PreserveAspectFit }
                            StudioTextArea { Layout.fillWidth: true; Layout.preferredHeight: 100; readOnly: true; text: page.originalText; color: Theme.textDim; wrapMode: TextEdit.Wrap }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Staged replacement"); color: Theme.textDim }
                            Image { Layout.fillWidth: true; Layout.preferredHeight: 150; source: page.comparisonImage; fillMode: Image.PreserveAspectFit }
                            StudioTextArea { Layout.fillWidth: true; Layout.preferredHeight: 100; readOnly: true; text: page.comparisonText; color: Theme.textDim; wrapMode: TextEdit.Wrap }
                        }
                    }
                }
            }
            Card {
                Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24
                ColumnLayout {
                    width: parent.width; spacing: 10
                    Label { text: qsTr("Batch plan / conflict checks / update protection"); color: Theme.text; font.bold: true }
                    Label { Layout.fillWidth: true; color: Theme.textDim; wrapMode: Text.Wrap; text: qsTr("Theme, resource and appearance edits commit as one EXE replacement. Boot assets, DLLs and GIF RPC settings remain separate operations: no cross-file atomicity is claimed. Any EXE update invalidates the plan.") }
                    RowLayout {
                        ActionButton { text: qsTr("Queue staged theme"); enabled: !backend.busy; onClicked: page.run("queue-theme") }
                        ActionButton { text: qsTr("Clear batch"); enabled: !backend.busy; onClicked: page.run("clear") }
                        ActionButton { text: qsTr("Dry Run"); enabled: backend.workspaceOperations.length > 0 && !backend.busy; onClicked: page.plan = page.run("dry-run") }
                        ActionButton { text: qsTr("Review & Apply"); accent: true; enabled: !!page.plan.ok && !backend.busy; onClicked: applyDialog.open() }
                    }
                    Repeater {
                        model: backend.workspaceOperations
                        Rectangle {
                            required property var modelData
                            required property int index
                            readonly property var summary: Catalog.operation(modelData)
                            Layout.fillWidth: true; implicitHeight: operationInfo.implicitHeight + 22
                            radius: 10; color: Theme.cardLow; border.color: Theme.border
                            ColumnLayout {
                                id: operationInfo
                                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 11; spacing: 4
                                Label { Layout.fillWidth: true; color: Theme.text; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; text: (index + 1) + ". " + summary.title }
                                Label { Layout.fillWidth: true; color: Theme.textDim; textFormat: Text.PlainText; wrapMode: Text.Wrap; text: summary.detail }
                                Label {
                                    Layout.fillWidth: true; wrapMode: Text.Wrap; textFormat: Text.PlainText
                                    color: page.plan.steps && page.plan.steps[index] && page.plan.steps[index].ok ? Theme.accentText : Theme.textDim
                                    text: page.plan.steps && page.plan.steps[index] ? page.plan.steps[index].msg : qsTr("Pending Dry Run — not applied")
                                }
                            }
                        }
                    }
                    StudioTextArea { Layout.fillWidth: true; readOnly: true; selectByMouse: true; color: Theme.textDim; wrapMode: TextEdit.Wrap; text: page.plan.msg ? JSON.stringify(page.plan, null, 2) : "" }
                }
            }
            Card {
                Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24
                ColumnLayout {
                    width: parent.width; spacing: 10
                    Label { text: qsTr("Recovery center"); color: Theme.text; font.bold: true }
                    ActionButton { text: qsTr("Refresh patch history"); enabled: !backend.busy; onClicked: { const r = page.run("history"); page.historyRows = r.entries || []; } }
                    StudioComboBox { id: recoveryChoice; Layout.fillWidth: true; model: page.historyRows; textRole: "id" }
                    StudioTextArea { Layout.fillWidth: true; readOnly: true; selectByMouse: true; color: Theme.textDim; wrapMode: TextEdit.Wrap; text: recoveryChoice.currentIndex >= 0 ? JSON.stringify(page.historyRows[recoveryChoice.currentIndex], null, 2) : qsTr("Workspace batches retain hash-verified recovery snapshots in AppData. Earlier editor operations still use their original .bak backups.") }
                    ActionButton { text: qsTr("Restore selected snapshot…"); enabled: recoveryChoice.currentIndex >= 0 && !backend.busy; onClicked: restoreDialog.open() }
                }
            }
            Card {
                Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24
                ColumnLayout {
                    width: parent.width; spacing: 10
                    Label { text: qsTr("Image preparation (never silently applied)"); color: Theme.text; font.bold: true }
                    RowLayout {
                        ActionButton { text: qsTr("Source image…"); onClicked: sourceImageDialog.open() }
                        Label { Layout.fillWidth: true; text: page.imageSource; color: Theme.textDim; elide: Text.ElideMiddle }
                    }
                    RowLayout {
                        Label { text: qsTr("Width / height"); color: Theme.textDim }
                        StudioSpinBox { id: imageWidth; from: 1; to: 8192; value: 512; editable: true }
                        StudioSpinBox { id: imageHeight; from: 1; to: 8192; value: 512; editable: true }
                        StudioCheckBox { id: imageCrop; text: qsTr("Center crop"); checked: false }
                        Label { text: qsTr("Quality"); color: Theme.textDim }
                        StudioSpinBox { id: quality; from: 1; to: 100; value: 95; editable: true }
                    }
                    Label { Layout.fillWidth: true; color: Theme.textDim; wrapMode: Text.Wrap; text: qsTr("Fit preserves aspect ratio; crop fills the requested size. PNG/WebP preserve transparency. JPEG export is refused when it would discard alpha. Animation is not preserved by image preparation.") }
                    ActionButton { text: qsTr("Prepare & save new image…"); enabled: page.imageSource !== "" && !backend.busy; onClicked: preparedImageDialog.open() }
                    RowLayout {
                        Image { Layout.fillWidth: true; Layout.preferredHeight: 150; source: page.imageSourceUrl; fillMode: Image.PreserveAspectFit }
                        Image { Layout.fillWidth: true; Layout.preferredHeight: 150; source: page.preparedImage; fillMode: Image.PreserveAspectFit }
                    }
                }
            }
            Card {
                Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24
                ColumnLayout {
                    width: parent.width; spacing: 10
                    Label { text: qsTr("Boot video preparation assistant"); color: Theme.text; font.bold: true }
                    RowLayout {
                        ActionButton { text: qsTr("Inspect MP4…"); enabled: !backend.busy; onClicked: videoDialog.open() }
                        ActionButton { text: qsTr("Play / decoder check"); enabled: page.videoUrl.toString() !== ""; onClicked: videoPlayer.play() }
                        ActionButton { text: qsTr("Pause"); onClicked: videoPlayer.pause() }
                        StudioCheckBox { id: videoCrop; text: qsTr("Crop preview"); checked: true }
                        ActionButton { text: qsTr("Boot editor →"); onClicked: page.navigate(5) }
                    }
                    VideoOutput { id: videoOutput; Layout.fillWidth: true; Layout.preferredHeight: 220; fillMode: videoCrop.checked ? VideoOutput.PreserveAspectCrop : VideoOutput.PreserveAspectFit }
                    Label { Layout.fillWidth: true; color: Theme.textDim; wrapMode: Text.Wrap; text: qsTr("Muted, looping preview uses this customizer's decoder, NOT the emulator's. Inspect reports MP4 metadata; actual target playback still needs verification. No bundled encoder or automatic conversion.") }
                }
            }
            Card {
                Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24
                ColumnLayout {
                    width: parent.width; spacing: 10
                    Label { text: qsTr("GIF RPC connection diagnostics"); color: Theme.text; font.bold: true }
                    RowLayout {
                        ActionButton { text: qsTr("Check saved profile / Discord IPC"); enabled: !backend.busy; onClicked: page.run("rpc") }
                        ActionButton { text: qsTr("GIF RPC editor →"); onClicked: page.navigate(8) }
                    }
                    Label { Layout.fillWidth: true; color: Theme.textDim; wrapMode: Text.Wrap; text: qsTr("Reachability and proxy-file inspection are separate from a successful activity connection. The tool does not claim live GIF animation or server acceptance without observing it.") }
                }
            }
            StudioTextArea { Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; text: page.report; color: Theme.text }
            Item { Layout.preferredHeight: 24 }
        }
    }
    FileDialog { id: reportDialog; title: qsTr("Save diagnostic report"); fileMode: FileDialog.SaveFile; defaultSuffix: "txt"; nameFilters: ["Text reports (*.txt)"]; onAccepted: page.run("diagnostics", {file: backend.localFilePath(selectedFile)}) }
    FileDialog { id: loadProfileDialog; nameFilters: ["Profiles (*.json)"]; onAccepted: page.run("profile-load", {file: backend.localFilePath(selectedFile)}) }
    FileDialog { id: saveProfileDialog; fileMode: FileDialog.SaveFile; defaultSuffix: "json"; nameFilters: ["Profiles (*.json)"]; onAccepted: page.run("profile-save", {file: backend.localFilePath(selectedFile), name: profileName.text}) }
    FileDialog {
        id: replacementDialog
        onAccepted: {
            const row = page.assets[resourceChoice.currentIndex];
            if (!row) return;
            page.originalImage = backend.resourcePreviewAt(row.index, 384);
            page.originalText = row.text ? backend.resourceTextAt(row.index).slice(0, 16384) : "";
            const r = page.run("queue-resource", {path: row.path, file: backend.localFilePath(selectedFile)});
            page.comparisonImage = r.preview || ""; page.comparisonText = r.text || "";
        }
    }
    FileDialog { id: sourceImageDialog; nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp)"]; onAccepted: { page.imageSource = backend.localFilePath(selectedFile); page.imageSourceUrl = selectedFile; page.preparedImage = ""; } }
    FileDialog {
        id: preparedImageDialog; fileMode: FileDialog.SaveFile; defaultSuffix: "png"; nameFilters: ["PNG (*.png)", "JPEG (*.jpg)", "WebP (*.webp)"]
        onAccepted: { const r = page.run("image", {source: page.imageSource, file: backend.localFilePath(selectedFile), width: imageWidth.value, height: imageHeight.value, crop: imageCrop.checked, quality: quality.value}); page.preparedImage = r.preview || ""; }
    }
    FileDialog { id: videoDialog; nameFilters: ["MP4 video (*.mp4)"]; onAccepted: { const r = page.run("video", {file: backend.localFilePath(selectedFile)}); if (r.ok) page.videoUrl = selectedFile; } }
    Dialog {
        id: applyDialog; objectName: "workspaceReviewDialog"; anchors.centerIn: parent; modal: true; width: Math.min(520, page.width - 32)
        title: qsTr("Review verified changes")
        property string reviewedPath: ""
        property string reviewedOutput: ""
        onOpened: {
            reviewedPath = backend.exePath;
            reviewedOutput = page.plan.outputSha256 || "";
            reviewAcknowledged.checked = false;
        }
        function stillValid() {
            return !!page.plan.ok && !!reviewedOutput && reviewedOutput === page.plan.outputSha256
                && reviewedPath === backend.exePath && !backend.busy;
        }
        onAboutToShow: reviewAcknowledged.checked = false
        height: Math.min(640, Overlay.overlay ? Overlay.overlay.height - 40 : 640)
        contentItem: ScrollView {
            contentWidth: availableWidth; clip: true
            ColumnLayout {
                width: parent.width; spacing: 12
                Label { Layout.fillWidth: true; text: qsTr("Target: ") + applyDialog.reviewedPath; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; color: Theme.text }
                Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textDim; text: backend.workspaceOperations.length + qsTr(" queued operation(s). This transaction changes one EXE only; DLLs, boot assets and RPC settings are separate.") }
                Label { Layout.fillWidth: true; color: Theme.text; text: qsTr("Size: ") + Catalog.bytes(page.plan.bytesBefore) + " → " + Catalog.bytes(page.plan.bytesAfter) }
                Label { Layout.fillWidth: true; text: qsTr("Before SHA-256: ") + (page.plan.sha256 || qsTr("Not verified")); textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; color: Theme.textDim; font.family: Theme.monoFont; font.pixelSize: 11 }
                Label { Layout.fillWidth: true; text: qsTr("Prepared SHA-256: ") + applyDialog.reviewedOutput; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; color: Theme.textDim; font.family: Theme.monoFont; font.pixelSize: 11 }
                Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textDim; text: qsTr("A hash-verified recovery snapshot is required before committing. The engine rechecks the target and prepared output; an emulator update or changed batch blocks Apply.") }
                StudioCheckBox { Layout.fillWidth: true; id: reviewAcknowledged; objectName: "workspaceReviewAcknowledged"; text: qsTr("I reviewed the staged changes and closed all player instances.") }
                Repeater {
                    model: backend.workspaceOperations
                    Label {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; color: Theme.textDim; textFormat: Text.PlainText
                        text: (index + 1) + ". " + Catalog.operation(modelData).title
                    }
                }
            }
        }
        footer: RowLayout {
            spacing: 10
            Item { Layout.fillWidth: true }
            ActionButton { text: qsTr("Cancel"); onClicked: applyDialog.reject() }
            ActionButton {
                objectName: "workspaceConfirmApply"
                text: qsTr("Apply verified batch"); accent: true
                enabled: reviewAcknowledged.checked && applyDialog.stillValid()
                onClicked: applyDialog.accept()
            }
            Item { Layout.preferredWidth: 12 }
        }
        onAccepted: {
            if (reviewAcknowledged.checked && stillValid()) page.run("apply", {outputSha256: reviewedOutput});
        }
    }
    Dialog {
        id: restoreDialog; objectName: "workspaceRestoreDialog"
        anchors.centerIn: parent; modal: true; width: Math.min(500, page.width - 32)
        height: Math.min(460, Overlay.overlay ? Overlay.overlay.height - 40 : 460)
        property string snapshotId: ""
        property string reviewedTarget: ""
        title: qsTr("Restore this recovery snapshot?"); standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            const row = page.historyRows[recoveryChoice.currentIndex];
            snapshotId = row ? row.id : "";
            reviewedTarget = backend.exePath;
        }
        contentItem: ScrollView {
            contentWidth: availableWidth; clip: true
            Label {
                width: parent.width; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText
                text: qsTr("Target: ") + restoreDialog.reviewedTarget + qsTr("\nSnapshot: ") + restoreDialog.snapshotId
                    + qsTr("\n\nOnly the exact selected EXE with the recorded post-patch hash may be restored. Close all player instances. Updates and unrelated modifications block automatic restore.")
            }
        }
        onAccepted: {
            if (!backend.busy && snapshotId && reviewedTarget === backend.exePath) page.run("restore", {id: snapshotId});
        }
    }
}
