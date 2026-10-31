import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Item {
    id: page
    property string report: qsTr("Select HD-Player.exe, check its files, then install/configure the RPC-only DLL.")
    property var status: ({})
    function show(result) { page.report = result.msg || ""; }
    function checkFiles() {
        const r = backend.gifRpcInspect(target.text);
        page.status = r;
        page.show(r);
        if (!r.ok || !r.installed) return;
        const s = r.settings;
        appId.text = s.AppId || ""; stateField.text = s.State || "";
        detailField.text = s.Details || ""; largeImage.text = s.LargeImage || "";
        largeText.text = s.LargeText || ""; smallImage.text = s.SmallImage || "";
        smallText.text = s.SmallText || ""; button1.text = s.Button1Label || "";
        url1.text = s.Button1Url || ""; button2.text = s.Button2Label || ""; url2.text = s.Button2Url || "";
        rpcEnabled.checked = s.Enabled; timestamp.checked = s.Timestamp; rotation.value = s.Interval || 5;
    }
    function configuration() {
        return {AppId: appId.text.trim(), State: stateField.text, Details: detailField.text,
                LargeImage: largeImage.text.trim(), LargeText: largeText.text,
                SmallImage: smallImage.text.trim(), SmallText: smallText.text,
                Button1Label: button1.text, Button1Url: url1.text.trim(),
                Button2Label: button2.text, Button2Url: url2.text.trim(),
                Enabled: rpcEnabled.checked, Timestamp: timestamp.checked, Interval: rotation.value};
    }
    property string lastProfileRpc: JSON.stringify((backend.customizationProfile || {}).gifRpc || {})
    Connections {
        target: backend
        function onCustomizationProfileChanged() {
            const s = (backend.customizationProfile || {}).gifRpc || {};
            const value = JSON.stringify(s); if (value === page.lastProfileRpc) return;
            page.lastProfileRpc = value;
            appId.text = s.AppId || ""; stateField.text = s.State || ""; detailField.text = s.Details || "";
            largeImage.text = s.LargeImage || ""; largeText.text = s.LargeText || "";
            smallImage.text = s.SmallImage || ""; smallText.text = s.SmallText || "";
            button1.text = s.Button1Label || ""; url1.text = s.Button1Url || "";
            button2.text = s.Button2Label || ""; url2.text = s.Button2Url || "";
            rpcEnabled.checked = s.Enabled !== false; timestamp.checked = !!s.Timestamp; rotation.value = s.Interval || 5;
        }
    }
    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: parent.width
            spacing: 14
            Text { text: qsTr("GIF RPC"); color: Theme.text; font.pixelSize: 24; font.bold: true; Layout.margins: 24 }
            Label {
                Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24
                color: Theme.textDim; wrapMode: Text.Wrap
                text: qsTr("A clean RPC-only version.dll for Windows x64 HD-Player. No optimizer, RAM cleaner, hotkeys, overlays or window hooks. Discord Desktop must be running. GIF animation/external image URLs depend on Discord; an asset key may display as a static image.")
            }
            Card {
                Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24
                ColumnLayout {
                    anchors.fill: parent; spacing: 12
                    PathRow { id: target; Layout.fillWidth: true; label: qsTr("HD-Player.exe"); text: backend.exePath; onLoad: page.checkFiles() }
                    ActionButton { text: qsTr("Save settings to profile"); enabled: !backend.busy; onClicked: page.show(backend.workspace("profile-section", {section: "gifRpc", value: page.configuration()})) }
                    ActionButton { text: qsTr("Check DLL / Load settings"); enabled: !backend.busy; onClicked: page.checkFiles() }
                    Label { text: qsTr("Application ID (your Discord application)"); color: Theme.text }
                    TextField { id: appId; Layout.fillWidth: true; placeholderText: "17–20 digit application ID"; maximumLength: 20; selectByMouse: true }
                    Label { text: qsTr("State"); color: Theme.text }
                    TextField { id: stateField; Layout.fillWidth: true; maximumLength: 128; selectByMouse: true }
                    Label { text: qsTr("Rotating details — one per line, maximum 10"); color: Theme.text }
                    TextArea { id: detailField; Layout.fillWidth: true; Layout.preferredHeight: 90; placeholderText: qsTr("Playing BlueStacks\nCustom presence"); selectByMouse: true; wrapMode: TextEdit.Wrap }
                    Label { text: qsTr("Large image / GIF HTTPS URL or uploaded asset key"); color: Theme.text }
                    TextField { id: largeImage; Layout.fillWidth: true; selectByMouse: true; maximumLength: 2048 }
                    TextField { id: largeText; Layout.fillWidth: true; placeholderText: qsTr("Large image tooltip"); selectByMouse: true; maximumLength: 128 }
                    Label { text: qsTr("Small image HTTPS URL or asset key (optional)"); color: Theme.text }
                    TextField { id: smallImage; Layout.fillWidth: true; selectByMouse: true; maximumLength: 2048 }
                    TextField { id: smallText; Layout.fillWidth: true; placeholderText: qsTr("Small image tooltip"); selectByMouse: true; maximumLength: 128 }
                    Label { text: qsTr("Buttons (optional; each needs both a label and HTTPS URL)"); color: Theme.text }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField { id: button1; Layout.preferredWidth: 160; placeholderText: qsTr("Button 1 label"); maximumLength: 32 }
                        TextField { id: url1; Layout.fillWidth: true; placeholderText: "https://..."; selectByMouse: true; maximumLength: 2048 }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField { id: button2; Layout.preferredWidth: 160; placeholderText: qsTr("Button 2 label"); maximumLength: 32 }
                        TextField { id: url2; Layout.fillWidth: true; placeholderText: "https://..."; selectByMouse: true; maximumLength: 2048 }
                    }
                    RowLayout {
                        CheckBox { id: rpcEnabled; text: qsTr("Enable RPC"); checked: true }
                        CheckBox { id: timestamp; text: qsTr("Elapsed time"); checked: true }
                        Label { text: qsTr("Rotate every (seconds)"); color: Theme.text }
                        SpinBox { id: rotation; from: 5; to: 60; value: 5; editable: true }
                    }
                    CheckBox {
                        id: replaceExisting
                        text: qsTr("Replace an existing third-party app-local version.dll (original backup required)")
                        checked: false
                    }
                    Label {
                        Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textDim
                        text: qsTr("Only the selected emulator folder is changed, never Windows/System32. Stop HD-Player first. Existing DLLs are protected unless you explicitly allow replacement. The original backup is preserved; settings use bsi-gif-rpc.ini beside the DLL. Your HD-Player build must actually load its app-local version.dll.")
                    }
                    RowLayout {
                        ActionButton {
                            text: qsTr("Install / Apply GIF RPC"); accent: true; enabled: !backend.busy
                            onClicked: {
                                page.show(backend.gifRpcApply(target.text, {
                                    AppId: appId.text.trim(), State: stateField.text, Details: detailField.text,
                                    LargeImage: largeImage.text.trim(), LargeText: largeText.text,
                                    SmallImage: smallImage.text.trim(), SmallText: smallText.text,
                                    Button1Label: button1.text, Button1Url: url1.text.trim(),
                                    Button2Label: button2.text, Button2Url: url2.text.trim(),
                                    Enabled: rpcEnabled.checked, Timestamp: timestamp.checked, Interval: rotation.value
                                }, replaceExisting.checked));
                            }
                        }
                        ActionButton { text: qsTr("Remove / Restore original"); enabled: !backend.busy; onClicked: restoreDialog.open() }
                    }
                    TextArea { Layout.fillWidth: true; text: page.report; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; color: Theme.text }
                }
            }
            Item { Layout.preferredHeight: 24 }
        }
    }
    Dialog {
        id: restoreDialog; anchors.centerIn: parent; modal: true; width: 430
        title: qsTr("Remove GIF RPC?"); standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { wrapMode: Text.Wrap; text: qsTr("Close HD-Player first. Only our RPC DLL is removed, or the preserved original is restored. Saved RPC settings and backups remain.") }
        onAccepted: page.show(backend.gifRpcRestore(target.text))
    }
}
