import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import QtWebEngine
import QtWebChannel
import BSICustomizer

// Opt-in Chromium shell. The existing login, updater and full native editors
// are reused, not reimplemented or bypassed by JavaScript.
ApplicationWindow {
    id: window
    width: 1480
    height: 920
    minimumWidth: 980
    minimumHeight: 640
    visible: true
    title: qsTr("BSI Customizer — Chromium workspace")
    color: Theme.window
    property bool booted: false
    property int nativePage: -1
    readonly property var nativeSources: ["DashboardPage.qml", "ThemePage.qml", "ResourcesPage.qml",
        "FpsPage.qml", "IntegrityPage.qml", "BootVideoPage.qml", "UiPage.qml", "RpcidPage.qml",
        "AppearancePage.qml", "AboutPage.qml", "GifRpcPage.qml", "WorkspacePage.qml", "DebloatPage.qml"]
    onClosing: (close) => { if (backend.busy) close.accepted = false; }

    QtObject {
        id: rpc
        WebChannel.id: "bsi"
        function snapshot() { return webBridge.snapshot(); }
        function invoke(method, args) { return webBridge.invoke(method, args); }
        function requestFile(role) { webBridge.requestFile(role); }
        signal stateChanged(var state)
        signal logMessage(string message, string color)
        signal fileSelected(string role, string path)
    }
    WebChannel { id: channel; registeredObjects: [rpc] }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        visible: auth.authorized
        UpdateBanner { Layout.fillWidth: true; Layout.preferredHeight: visible ? implicitHeight : 0 }
        Rectangle {
            visible: window.nativePage >= 0
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 48 : 0
            color: Theme.topbar
            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                Button { text: qsTr("← Back to Chromium workspace"); enabled: !backend.busy; onClicked: window.nativePage = -1 }
                Button { text: qsTr("Debloat / Root.vhd tools"); enabled: !backend.busy; onClicked: window.nativePage = 12 }
                Label { text: qsTr("Full native editor · same session and C++ backend"); color: Theme.text; Layout.fillWidth: true }
            }
        }
        Loader {
            id: browserLoader
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: auth.authorized
            visible: window.nativePage < 0
            sourceComponent: WebEngineView {
                id: browser
                objectName: "chromiumWorkspace"
                url: "qrc:/bsi-web/index.html"
                webChannel: channel
                settings.localContentCanAccessRemoteUrls: false
                settings.localContentCanAccessFileUrls: false
                settings.javascriptCanOpenWindows: false
                settings.pluginsEnabled: false
                onNavigationRequested: (request) => {
                    if (request.url.toString() !== "qrc:/bsi-web/index.html")
                        request.action = WebEngineNavigationRequest.IgnoreRequest;
                }
                onLoadingChanged: (request) => {
                    if (request.status === WebEngineView.LoadFailedStatus)
                        toast.show(qsTr("Chromium UI failed to load: ") + request.errorString);
                }
                onRenderProcessTerminated: (status, exitCode) => {
                    toast.show(qsTr("Chromium renderer stopped. Restart with --native-ui if this persists."));
                }
            }
        }
        Loader {
            id: nativeLoader
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: active ? 20 : 0
            active: auth.authorized && window.nativePage >= 0
            visible: active
            source: active ? "pages/" + window.nativeSources[window.nativePage] : ""
        }
        Connections {
            target: nativeLoader.item
            ignoreUnknownSignals: true
            function onNavigate(index) { window.nativePage = index; }
            function onNotify(message) { toast.show(message); }
        }
        LogPanel { id: logs; visible: window.nativePage >= 0; Layout.fillWidth: true; Layout.preferredHeight: visible ? 130 : 0 }
    }
    FileDialog {
        id: fileDialog
        property string roleName: ""
        title: roleName === "extract" ? qsTr("Extract resource to…") : qsTr("Select a local file")
        fileMode: ["extract", "profile-save", "report-save", "image-save"].includes(roleName) ? FileDialog.SaveFile : FileDialog.OpenFile
        onAccepted: webBridge.fileChosen(roleName, selectedFile)
        onRejected: webBridge.fileChosen(roleName, "")
    }
    Connections {
        target: webBridge
        function onStateChanged(state) { rpc.stateChanged(state); }
        function onLogMessage(message, color) { rpc.logMessage(message, color); logs.append(message, color); }
        function onFileSelected(role, path) { rpc.fileSelected(role, path); }
        function onOpenFileRequested(role) { fileDialog.roleName = role; fileDialog.open(); }
        function onNativeToolRequested(page) { window.nativePage = page; }
    }
    Connections {
        target: auth
        function onAuthorizedChanged() {
            if (auth.authorized && !window.booted) {
                window.booted = true;
                backend.detectInstalls();
            }
            if (!auth.authorized) { fileDialog.close(); window.nativePage = -1; }
        }
    }
    LoginGate { anchors.fill: parent; appWindow: window; visible: !auth.authorized; enabled: visible; z: 100 }
    Toast { id: toast; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 24; z: 200 }
}
