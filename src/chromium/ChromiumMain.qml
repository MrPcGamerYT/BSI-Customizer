import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import QtQuick.Layouts
import QtMultimedia
import QtQuick.Dialogs
import QtWebEngine
import QtWebChannel
import BSICustomizer

// Production HTML shell. Existing login, updater and engines remain native;
// the original QML editors are retained only for explicit recovery.
ApplicationWindow {
    id: window
    width: 1480
    height: 920
    minimumWidth: 980
    minimumHeight: 640
    visible: true
    flags: Qt.Window | Qt.FramelessWindowHint
    title: qsTr("BSI Customizer — Chromium workspace")
    color: Theme.window
    property bool booted: false
    property int nativePage: -1
    signal retryBrowser()
    property var videoRect: ({x:0, y:0, width:0, height:0, visible:false})
    property bool videoCrop: true
    // Only the video pixels are native. All controls and layout stay in HTML;
    // this is the original Qt Multimedia decoder, not a different HTML codec.
    MediaPlayer {
        id: bootPreview
        audioOutput: AudioOutput { muted: true }
        loops: MediaPlayer.Infinite
        onErrorOccurred: (error, message) => webBridge.videoStatus(message)
        onPlaybackStateChanged: {
            webBridge.videoStatus(playbackState === MediaPlayer.PlayingState
                ? qsTr("Playing with this app's Qt decoder. Emulator playback is not verified.")
                : qsTr("Preview paused / stopped. Emulator playback is not verified."))
        }
    }
    Connections {
        target: webBridge
        function onVideoFileRequested(file) { bootPreview.stop(); bootPreview.source = file; }
        function onVideoControlRequested(action, options) {
            if (action === "geometry") { window.videoRect = options; if (!options.visible) bootPreview.pause(); }
            else if (action === "play") bootPreview.play();
            else if (action === "pause") bootPreview.pause();
            else if (action === "crop") window.videoCrop = options.crop;
        }
    }
    Connections {
        target: backend
        function onBusyChanged() { if (backend.busy) bootPreview.pause(); }
    }
    property bool browserReady: false
    property string browserFailure: ""
    readonly property var nativeSources: ["DashboardPage.qml", "ThemePage.qml", "ResourcesPage.qml",
        "FpsPage.qml", "IntegrityPage.qml", "BootVideoPage.qml", "UiPage.qml", "RpcidPage.qml",
        "AppearancePage.qml", "AboutPage.qml", "GifRpcPage.qml", "WorkspacePage.qml", "DebloatPage.qml"]
    onClosing: (close) => { if (backend.busy) close.accepted = false; }

    QtObject {
        id: rpc
        WebChannel.id: "bsi"
        function windowAction(action) {
            if (action === "min") window.showMinimized();
            else if (action === "max") {
                if (window.visibility === Window.Maximized) window.showNormal();
                else window.showMaximized();
            } else if (action === "close") window.close();
            else if (action === "move") window.startSystemMove();
        }
        function snapshot() { return webBridge.snapshot(); }
        function invoke(method, args) { return webBridge.invoke(method, args); }
        function requestFile(role) { webBridge.requestFile(role); }
        signal stateChanged(var state)
        signal logMessage(string message, string color)
        signal fileSelected(string role, string path)
        signal videoStatus(string message)
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
                Label { text: qsTr("Full native editor"); color: Theme.text; Layout.fillWidth: true }
                WinControls { appWindow: window }
            }
        }
        Loader {
            id: browserLoader
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: window.browserReady
            visible: window.nativePage < 0 && !window.browserFailure
            sourceComponent: WebEngineView {
                id: browser
                objectName: "chromiumWorkspace"
                url: "qrc:/bsi-web/index.html"
                webChannel: channel
                settings.localContentCanAccessRemoteUrls: false
                settings.localContentCanAccessFileUrls: false
                settings.javascriptCanOpenWindows: false
                settings.pluginsEnabled: false
                clip: true
                onVisibleChanged: if (!visible) bootPreview.pause()
                VideoOutput {
                    id: bootVideoPixels
                    z: 10
                    x: window.videoRect.x; y: window.videoRect.y
                    width: Math.max(0, window.videoRect.width)
                    height: Math.max(0, window.videoRect.height)
                    visible: auth.authorized && window.nativePage < 0 && !backend.busy && !window.browserFailure && window.videoRect.visible
                    fillMode: window.videoCrop ? VideoOutput.PreserveAspectCrop : VideoOutput.PreserveAspectFit
                    Component.onCompleted: bootPreview.videoOutput = bootVideoPixels
                    Component.onDestruction: { if (bootPreview) { bootPreview.pause(); bootPreview.videoOutput = null; } }
                }
                Connections {
                    target: window
                    function onRetryBrowser() { browser.reload(); }
                }
                onNavigationRequested: (request) => {
                    if (request.url.toString() !== "qrc:/bsi-web/index.html")
                        request.action = WebEngineNavigationRequest.IgnoreRequest;
                }
                onLoadingChanged: (request) => {
                    if (request.status === WebEngineView.LoadFailedStatus)
                        window.browserFailure = qsTr("The workspace could not load. You can retry or open the native tools.");
                }
                onRenderProcessTerminated: (status, exitCode) => {
                    window.browserFailure = qsTr("The display process stopped (code %1). Your sign-in is still protected. Check any in-progress operation before retrying.").arg(exitCode);
                }
            }
        }
        Rectangle {
            visible: auth.authorized && window.nativePage < 0 && !!window.browserFailure
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.window
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 560)
                spacing: 20
                Label { text: qsTr("Let’s get you back."); font.pixelSize: 26; color: Theme.text }
                Label { text: window.browserFailure; wrapMode: Text.WordWrap; Layout.fillWidth: true; color: Theme.textDim }
                Button {
                    text: qsTr("Reload workspace"); enabled: !backend.busy
                    onClicked: { window.browserFailure = ""; window.retryBrowser(); }
                }
                Button { text: qsTr("Open native tools"); enabled: !backend.busy; onClicked: window.nativePage = 0 }
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
        fileMode: ["extract", "profile-save", "report-save", "image-save", "disk-extract"].includes(roleName) ? FileDialog.SaveFile : FileDialog.OpenFile
        onAccepted: webBridge.fileChosen(roleName, selectedFile)
        onRejected: webBridge.fileChosen(roleName, "")
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("Choose a local folder")
        onAccepted: webBridge.fileChosen("disk-folder", selectedFolder)
        onRejected: webBridge.fileChosen("disk-folder", "")
    }
    Connections {
        target: webBridge
        function onStateChanged(state) { rpc.stateChanged(state); }
        function onLogMessage(message, color) { rpc.logMessage(message, color); logs.append(message, color); }
        function onVideoStatus(message) { rpc.videoStatus(message); }
        function onFileSelected(role, path) { rpc.fileSelected(role, path); }
        function onOpenFileRequested(role) {
            if (role === "disk-folder") { folderDialog.open(); return; }
            fileDialog.roleName = role; fileDialog.open();
        }
        function onNativeToolRequested(page) { window.nativePage = page; }
    }
    Connections {
        target: auth
        function onAuthorizedChanged() {
            if (auth.authorized && !window.booted) {
                window.booted = true;
                backend.detectInstalls();
            }
            if (!auth.authorized) { bootPreview.stop(); bootPreview.source = ""; window.videoRect = ({x:0,y:0,width:0,height:0,visible:false}); folderDialog.close(); fileDialog.close(); window.nativePage = -1; }
            // Do not destroy WebEngineView inside a WebChannel invocation
            // (e.g. sign-out). Let the native call and signal delivery unwind.
            Qt.callLater(function() {
                window.browserReady = auth.authorized;
                if (!auth.authorized) window.browserFailure = "";
            });
        }
    }
    LoginGate { anchors.fill: parent; appWindow: window; visible: !auth.authorized; enabled: visible; z: 100 }
    Toast { id: toast; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 24; z: 200 }
}
