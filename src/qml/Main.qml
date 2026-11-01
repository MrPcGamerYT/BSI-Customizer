import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import BSICustomizer
import "Icons.js" as Icons

ApplicationWindow {
    onClosing: (close) => { if (backend.busy) close.accepted = false; }

    id: window
    width: 1240
    height: 780
    minimumWidth: 980
    minimumHeight: 640
    visible: true
    // Premium-Optimizer chrome: frameless window with the app's own topbar and
    // control box (startSystemMove / startSystemResize keep native snap+resize).
    flags: Qt.Window | Qt.FramelessWindowHint
    title: qsTr("BlueStacks 5 / MSI 5 — BSI Customizer")
    color: Theme.window
    font.family: "Segoe UI"
    font.pixelSize: 13
    palette.window: Theme.window
    palette.windowText: Theme.text
    palette.text: Theme.text
    palette.base: Theme.cardLow
    palette.button: Theme.card
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.primaryText
    palette.placeholderText: Theme.textDim

    Dialog {
        id: purgeCacheDialog
        anchors.centerIn: parent
        title: qsTr("Purge HD-Player cache?")
        width: 440
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label {
            text: qsTr("Close all emulator instances first. This removes only HD-Player cache folders in AppData. Settings, installed apps and emulator disks are kept.")
            wrapMode: Text.Wrap
        }
        onAccepted: {
            const r = backend.purgePlayerCache();
            toast.show(r.msg || qsTr("Cache purge finished."));
        }
    }

    // ---- probes used by the C++ --selftest (do not remove) ----
    property color testProbe: Theme.window
    property color testProbeAccent: Theme.accent

    property bool booted: false

    // The studio shell uses the existing page stack and live context objects.
    readonly property var pageNames: [qsTr("Overview"), qsTr("Theme studio"), qsTr("Resource explorer"),
        qsTr("Performance"), qsTr("Integrity & safety"), qsTr("Boot experience"), qsTr("Player interface"),
        qsTr("Discord presence"), qsTr("Appearance"), qsTr("About BSI"), qsTr("Animated presence"), qsTr("Patch workspace"), qsTr("Debloat")]

    Rectangle {
        id: topbar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 64
        color: Theme.topbar
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }

        MouseArea {
            anchors.fill: parent
            anchors.rightMargin: 350
            onPressed: window.startSystemMove()
            onDoubleClicked: {
                if (window.visibility === Window.Maximized) window.showNormal();
                else window.showMaximized();
            }
        }
        RowLayout {
            anchors.fill: parent
            spacing: 16
            Item {
                Layout.preferredWidth: 224
                Layout.fillHeight: true
                Row {
                    anchors.centerIn: parent
                    spacing: 10
                    Rectangle {
                        width: 33; height: 35; radius: 10
                        color: Theme.accent
                        Text { anchors.centerIn: parent; text: "bi"; color: Theme.primaryText; font.pixelSize: 23; font.weight: Font.Bold; font.letterSpacing: -2 }
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        Text { text: qsTr("BSI Customizer"); color: Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                        Text { text: qsTr("YOUR PLAYER. YOUR PERSONALITY."); color: Theme.textDim; font.pixelSize: 8; font.letterSpacing: 1 }
                    }
                }
            }
            Text { text: qsTr("Workspace"); color: Theme.textDim; font.pixelSize: 11 }
            Text { text: "/"; color: Theme.textDim; font.pixelSize: 11 }
            Text { Layout.fillWidth: true; text: window.pageNames[pages.currentIndex]; color: Theme.text; font.pixelSize: 11; elide: Text.ElideRight }
            Rectangle { width: 6; height: 6; radius: 3; color: auth.authorized ? Theme.ok : Theme.warn }
            Text { text: auth.authorized ? qsTr("DEV ACCESS") : qsTr("SIGN IN REQUIRED"); color: Theme.textDim; font.pixelSize: 9; font.letterSpacing: 1 }
            ActionButton { text: qsTr("Appearance"); implicitHeight: 30; onClicked: pages.currentIndex = 8 }
            WinControls { appWindow: window; barHeight: 64 }
        }
    }

    RowLayout {
        id: shell
        anchors.top: topbar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 224
            objectName: "studioSidebar"
            color: Theme.sidebar
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 53
                    Layout.topMargin: 6
                    Layout.bottomMargin: 8
                    radius: 8
                    color: Theme.card
                    border.color: Theme.border
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 11; spacing: 9
                        Image { source: Icons.icon(Icons.extras, Theme.accent); sourceSize: Qt.size(18,18); Layout.preferredWidth: 18; Layout.preferredHeight: 18 }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 4
                            Text { text: qsTr("Personal workspace"); color: Theme.text; font.pixelSize: 11; font.weight: Font.DemiBold }
                            Text { Layout.fillWidth: true; text: backend.exePath ? qsTr("Player selected") : qsTr("Choose a player to begin"); color: Theme.textDim; font.pixelSize: 9; elide: Text.ElideRight }
                        }
                        Rectangle { width: 5; height: 5; radius: 3; color: backend.exePath ? Theme.ok : Theme.textDim }
                    }
                }
                Flickable {
                    id: navScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: width
                    contentHeight: navColumn.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    ColumnLayout {
                        id: navColumn
                        width: navScroll.width
                        spacing: 2
                        Repeater {
                            model: [
                                { header: true, label: qsTr("WORKSPACE") },
                                { page: 0, label: qsTr("Overview"), icon: Icons.dashboard },
                                { header: true, label: qsTr("CUSTOMIZATION") },
                                { page: 1, label: qsTr("Theme studio"), icon: Icons.theme },
                                { page: 2, label: qsTr("Resource explorer"), icon: Icons.images },
                                { page: 3, label: qsTr("Performance"), icon: Icons.fps },
                                { page: 5, label: qsTr("Boot experience"), icon: Icons.boot },
                                { page: 6, label: qsTr("Player interface"), icon: Icons.extras },
                                { header: true, label: qsTr("TOOLBOX") },
                                { page: 4, label: qsTr("Integrity & safety"), icon: Icons.integrity },
                                { page: 7, label: qsTr("Discord presence"), icon: Icons.discord },
                                { page: 10, label: qsTr("Animated presence"), icon: Icons.discord },
                                { page: 11, label: qsTr("Patch workspace"), icon: Icons.extras },
                                { page: 12, label: qsTr("Debloat"), icon: Icons.debloat },
                                { header: true, label: qsTr("APPLICATION") },
                                { page: 8, label: qsTr("Appearance"), icon: Icons.appearance },
                                { page: 9, label: qsTr("About BSI"), icon: Icons.about }
                            ]
                            delegate: Loader {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: modelData.header ? 25 : 36
                                sourceComponent: modelData.header ? navHeader : navButton
                                onLoaded: {
                                    if (modelData.header) item.labelText = modelData.label;
                                    else {
                                        item.label = modelData.label;
                                        item.iconPath = modelData.icon;
                                        item.active = Qt.binding(() => pages.currentIndex === modelData.page);
                                        item.clicked.connect(() => pages.currentIndex = modelData.page);
                                    }
                                }
                            }
                        }
                    }
                }
                ActionButton {
                    Layout.fillWidth: true
                    text: qsTr("Purge player cache")
                    enabled: !backend.busy
                    onClicked: purgeCacheDialog.open()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Clear HD-Player caches in AppData without deleting emulator data")
                }
                Rectangle {
                    visible: auth.authorized
                    Layout.fillWidth: true
                    Layout.preferredHeight: 57
                    color: "transparent"
                    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }
                    RowLayout {
                        anchors.fill: parent; anchors.topMargin: 9; spacing: 8
                        Rectangle {
                            width: 30; height: 30; radius: 9; color: Theme.accentSoft
                            Text { anchors.centerIn: parent; text: (auth.user.DisplayName || auth.user.Username || "?").charAt(0).toUpperCase(); textFormat: Text.PlainText; color: Theme.accentText; font.pixelSize: 13 }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 3
                            Text { Layout.fillWidth: true; text: auth.user.DisplayName || auth.user.Username || qsTr("Signed in"); textFormat: Text.PlainText; color: Theme.text; font.pixelSize: 11; elide: Text.ElideRight }
                            Text { text: qsTr("Authenticated workspace"); color: Theme.textDim; font.pixelSize: 9 }
                        }
                        ActionButton { text: qsTr("Sign out"); enabled: !backend.busy; implicitHeight: 27; onClicked: auth.signOut() }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            UpdateBanner {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.topMargin: visible ? 10 : 0
                Layout.preferredHeight: visible ? implicitHeight : 0
            }
            StackLayout {
                id: pages
                objectName: "studioPageStack"
                Layout.fillWidth: true
                Layout.fillHeight: true
                DashboardPage { onNavigate: (index) => pages.currentIndex = index }
                ThemePage {}
                ResourcesPage {}
                FpsPage {}
                IntegrityPage {}
                BootVideoPage {}
                UiPage { onNotify: (msg) => toast.show(msg) }
                RpcidPage {}
                AppearancePage { onNotify: (msg) => toast.show(msg) }
                AboutPage { onNotify: (msg) => toast.show(msg) }
                GifRpcPage {}
                WorkspacePage { onNavigate: (index) => pages.currentIndex = index }
                DebloatPage { onNotify: (msg) => toast.show(msg) }
            }
            LogPanel {
                id: logPanel
                Layout.fillWidth: true
                Layout.preferredHeight: logPanel.collapsed ? 40 : 150
                Behavior on Layout.preferredHeight { NumberAnimation { duration: 140 } }
            }
        }
    }

    // ------------------------------------------------------------------
    // Resize edges for the frameless window (native edges while maximized).
    // ------------------------------------------------------------------
    MouseArea {
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: 6
        enabled: window.visibility !== Window.Maximized
        cursorShape: Qt.SizeVerCursor
        onPressed: window.startSystemResize(Qt.TopEdge)
    }
    MouseArea {
        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
        height: 6
        enabled: window.visibility !== Window.Maximized
        cursorShape: Qt.SizeVerCursor
        onPressed: window.startSystemResize(Qt.BottomEdge)
    }
    MouseArea {
        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
        width: 6
        enabled: window.visibility !== Window.Maximized
        cursorShape: Qt.SizeHorCursor
        onPressed: window.startSystemResize(Qt.LeftEdge)
    }
    MouseArea {
        anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
        width: 6
        enabled: window.visibility !== Window.Maximized
        cursorShape: Qt.SizeHorCursor
        onPressed: window.startSystemResize(Qt.RightEdge)
    }
    MouseArea {
        anchors.left: parent.left; anchors.top: parent.top
        width: 12; height: 12
        enabled: window.visibility !== Window.Maximized
        cursorShape: Qt.SizeFDiagCursor
        onPressed: window.startSystemResize(Qt.LeftEdge | Qt.TopEdge)
    }
    MouseArea {
        anchors.right: parent.right; anchors.top: parent.top
        width: 12; height: 12
        enabled: window.visibility !== Window.Maximized
        cursorShape: Qt.SizeBDiagCursor
        onPressed: window.startSystemResize(Qt.RightEdge | Qt.TopEdge)
    }
    MouseArea {
        anchors.left: parent.left; anchors.bottom: parent.bottom
        width: 12; height: 12
        enabled: window.visibility !== Window.Maximized
        cursorShape: Qt.SizeBDiagCursor
        onPressed: window.startSystemResize(Qt.LeftEdge | Qt.BottomEdge)
    }
    MouseArea {
        anchors.right: parent.right; anchors.bottom: parent.bottom
        width: 12; height: 12
        enabled: window.visibility !== Window.Maximized
        cursorShape: Qt.SizeFDiagCursor
        onPressed: window.startSystemResize(Qt.RightEdge | Qt.BottomEdge)
    }

    // ------------------------------------------------------------------
    // The sign-in gate. The card is up first; nothing behind it is usable
    // until AuthManager verifies the Discord sign-in (and Backend refuses
    // its actions either way).
    // ------------------------------------------------------------------
    LoginGate {
        id: loginGate
        anchors.fill: parent
        appWindow: window
        z: 3200
        visible: opacity > 0.01
        enabled: !auth.authorized
        opacity: auth.authorized ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: 260 } }
    }

    // FalcoForge's boot, one to one: the gate closing starts the app's
    // first pulls - saved sign-in alone never does.
    Connections {
        target: auth
        function onAuthorizedChanged() {
            if (auth.authorized && !window.booted) {
                window.booted = true;
                backend.detectInstalls();
            }
        }
    }

    // Nav groups + buttons for the sidebar Repeater.
    Component {
        id: navHeader
        Item {
            property alias labelText: headerLabel.text
            height: 30
            Text {
                id: headerLabel
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.textDim
                font.pixelSize: 9
                font.weight: Font.DemiBold
                font.letterSpacing: 1.2
                opacity: 0.8
            }
        }
    }
    Component {
        id: navButton
        SidebarButton {
            Layout.fillWidth: true
        }
    }

    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
    }

    Connections {
        target: backend
        function onLog(message, color) { logPanel.append(message, color) }
    }

    // Modal while the isolated patch worker owns the selected files. The GUI
    // keeps rendering; keyboard shortcuts and repeat clicks cannot start writes.
    Popup {
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 340
        modal: true
        dim: true
        focus: true
        closePolicy: Popup.NoAutoClose
        visible: backend.busy
        padding: 24
        background: Rectangle {
            color: Theme.card
            radius: 16
            border.color: Theme.neonFaint
        }
        contentItem: ColumnLayout {
            spacing: 14
            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: backend.busy
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Working…")
                color: Theme.text
                font.pixelSize: 18
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Please wait while the operation finishes.\nDo not close the app or start BlueStacks.")
                color: Theme.textDim
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }
    }

}
