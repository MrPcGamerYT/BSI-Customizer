import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import BSICustomizer
import "Icons.js" as Icons

ApplicationWindow {
    onClosing: (close) => { if (backend.busy) close.accepted = false; }

    id: window
    width: 1480
    height: 920
    minimumWidth: 980
    minimumHeight: 640
    visible: true
    // Premium-Optimizer chrome: frameless window with the app's own topbar and
    // control box (startSystemMove / startSystemResize keep native snap+resize).
    flags: Qt.Window | Qt.FramelessWindowHint
    title: qsTr("BlueStacks 5 / MSI 5 — BSI Customizer")
    color: Theme.window
    font.family: "Plus Jakarta Sans"
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

    // ONE production shell. Every page below is the original QML editor;
    // no HTML command adapter, parallel form state or native-editor switch.
    readonly property var featurePages: [
        { label: qsTr("Dashboard"), icon: Icons.dashboard },
        { label: qsTr("Color studio"), icon: Icons.theme },
        { label: qsTr("Resources"), icon: Icons.images },
        { label: qsTr("Performance"), icon: Icons.fps },
        { label: qsTr("Integrity & safety"), icon: Icons.integrity },
        { label: qsTr("Boot experience"), icon: Icons.boot },
        { label: qsTr("Player interface"), icon: Icons.extras },
        { label: qsTr("Discord presence"), icon: Icons.discord },
        { label: qsTr("Animated presence"), icon: Icons.discord },
        { label: qsTr("Patch workspace"), icon: Icons.extras },
        { label: qsTr("Debloat / Android / Root.vhd"), icon: Icons.debloat }
    ]
    readonly property var pageNames: featurePages.map(p => p.label)
    property alias currentPage: pages.currentIndex
    function navigatePage(index) {
        if (!backend.busy && Number.isInteger(index) && index >= 0 && index < featurePages.length)
            pages.currentIndex = index;
    }

    Image {
        anchors.left: parent.left; anchors.top: parent.top
        width: parent.width * 0.8; height: parent.height
        source: "img/orb-green.png"; opacity: Theme.dark ? 0.24 : 0.06
    }
    Image {
        anchors.right: parent.right; anchors.bottom: parent.bottom
        width: parent.width * 0.6; height: parent.height * 0.85
        source: "img/orb-green.png"; opacity: Theme.dark ? 0.16 : 0.05
    }

    Rectangle {
        id: rail
        objectName: "studioSidebar"
        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
        width: 76
        color: Theme.dark ? "#55030a08" : "#55ffffff"
        visible: auth.authorized
        Rectangle { anchors.right: parent.right; height: parent.height; width: 1; color: Theme.border }
        Image { anchors.top: parent.top; anchors.topMargin: 20; anchors.horizontalCenter: parent.horizontalCenter; width: 38; height: 38; source: "img/app-icon.png" }
        ScrollView {
            anchors.top: parent.top; anchors.topMargin: 86
            anchors.bottom: railFooter.top; anchors.bottomMargin: 14
            anchors.left: parent.left; anchors.right: parent.right
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: rail.width
                spacing: 5
                Repeater {
                    model: window.featurePages
                    delegate: AbstractButton {
                        id: railButton
                        required property var modelData
                        required property int index
                        objectName: "featureNav" + index
                        Layout.alignment: Qt.AlignHCenter
                        implicitWidth: 44; implicitHeight: 42
                        enabled: !backend.busy
                        Accessible.name: modelData.label
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.label
                        background: Rectangle {
                            radius: 12
                            color: pages.currentIndex === railButton.index ? Qt.alpha(Theme.accent, 0.14) : railButton.hovered ? Qt.alpha(Theme.text, 0.06) : "transparent"
                            border.color: pages.currentIndex === railButton.index || railButton.activeFocus ? Qt.alpha(Theme.accent, 0.35) : "transparent"
                            Rectangle { visible: pages.currentIndex === railButton.index; width: 2; height: 20; radius: 1; x: -16; anchors.verticalCenter: parent.verticalCenter; color: Theme.accent }
                        }
                        contentItem: Image {
                            source: Icons.icon(railButton.modelData.icon, pages.currentIndex === railButton.index ? Theme.text : Theme.textDim)
                            sourceSize: Qt.size(19,19)
                            fillMode: Image.Pad
                        }
                        onClicked: window.navigatePage(index)
                    }
                }
            }
        }
        Text { id: railFooter; anchors.bottom: parent.bottom; anchors.bottomMargin: 18; anchors.horizontalCenter: parent.horizontalCenter; text: "BSI 3.0.1"; color: Theme.textDim; font.pixelSize: 8; font.letterSpacing: 1 }
    }

    Item {
        id: topbar
        anchors.left: rail.right; anchors.right: parent.right; anchors.top: parent.top
        anchors.leftMargin: 28; anchors.rightMargin: 22
        height: 86
        visible: auth.authorized
        MouseArea {
            anchors.fill: parent
            onPressed: window.startSystemMove()
            onDoubleClicked: window.visibility === Window.Maximized ? window.showNormal() : window.showMaximized()
        }
        RowLayout {
            anchors.fill: parent
            spacing: 16
            ColumnLayout {
                Layout.fillWidth: true; spacing: 4
                Text { text: qsTr("BSI Customizer"); color: Theme.text; font.family: Theme.headingFont; font.pixelSize: 16; font.weight: Font.Bold }
                Text { text: qsTr("Premium emulator control center"); color: Theme.textDim; font.pixelSize: 10 }
            }
            Rectangle {
                visible: window.width > 1380
                implicitWidth: quickNav.implicitWidth + 10; implicitHeight: 42
                radius: 13; color: Qt.alpha(Theme.window, 0.5); border.color: Theme.border
                RowLayout {
                    id: quickNav
                    anchors.centerIn: parent; spacing: 3
                    Repeater {
                        model: [0, 1, 6, 7, 10]
                        delegate: AbstractButton {
                            id: quickButton
                            required property int modelData
                            implicitWidth: quickLabel.implicitWidth + 22; implicitHeight: 32
                            enabled: !backend.busy
                            background: Rectangle { radius: 9; color: pages.currentIndex === quickButton.modelData ? Qt.alpha(Theme.accent, 0.12) : "transparent"; border.color: pages.currentIndex === quickButton.modelData || quickButton.activeFocus ? Theme.neonSoft : "transparent" }
                            contentItem: Text { id: quickLabel; text: window.featurePages[quickButton.modelData].label; color: pages.currentIndex === quickButton.modelData ? Theme.text : Theme.textDim; font.pixelSize: 10; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            onClicked: window.navigatePage(modelData)
                        }
                    }
                }
            }
            ComboBox {
                id: targetPicker
                objectName: "shellTargetPicker"
                Layout.preferredWidth: Math.min(210, window.width * 0.17)
                implicitHeight: 38
                enabled: !backend.busy
                model: backend.installs
                currentIndex: {
                    const found = backend.installs.findIndex(i => i.exe === backend.exePath);
                    return found;
                }
                textRole: "profile"
                displayText: currentText || qsTr("Select target")
                onActivated: backend.useInstall(currentIndex)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Choose an existing installation; detect players on Dashboard")
            }
            AbstractButton {
                id: settingsButton
                objectName: "settingsButton"
                implicitWidth: 38
                implicitHeight: 34
                enabled: !backend.busy
                Accessible.name: qsTr("Open settings")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Appearance, updates and About")
                background: Rectangle {
                    radius: 10
                    color: settingsButton.hovered ? Theme.accentSoft : "transparent"
                    border.color: settingsButton.hovered || settingsButton.activeFocus ? Theme.borderStrong : Theme.border
                }
                contentItem: Image {
                    source: Icons.icon(Icons.settings, settingsButton.hovered ? Theme.accentText : Theme.textDim)
                    sourceSize: Qt.size(18, 18)
                    fillMode: Image.Pad
                }
                onClicked: { settingsPanel.currentTab = 0; settingsPanel.open(); }
            }
            ActionButton { text: "⋯"; implicitWidth: 32; implicitHeight: 34; onClicked: appMenu.popup() }
            WinControls { appWindow: window; barHeight: 40 }
        }
    }
    Menu {
        id: appMenu
        MenuItem { text: qsTr("Purge player cache…"); enabled: !backend.busy; onTriggered: purgeCacheDialog.open() }
        MenuItem {
            text: qsTr("Settings")
            enabled: !backend.busy
            onTriggered: { settingsPanel.currentTab = 0; settingsPanel.open(); }
        }
        MenuItem {
            text: qsTr("Updates")
            enabled: !backend.busy
            onTriggered: { settingsPanel.currentTab = 1; settingsPanel.open(); }
        }
        MenuItem {
            text: qsTr("About")
            enabled: !backend.busy
            onTriggered: { settingsPanel.currentTab = 2; settingsPanel.open(); }
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Sign out"); enabled: !backend.busy; onTriggered: auth.signOut() }
    }

    ColumnLayout {
        id: shell
        anchors.top: topbar.bottom; anchors.bottom: parent.bottom
        anchors.left: rail.right; anchors.right: parent.right
        anchors.leftMargin: 20; anchors.rightMargin: 20; anchors.bottomMargin: 16
        visible: auth.authorized
        spacing: 10
        UpdateBanner { Layout.fillWidth: true; Layout.preferredHeight: visible ? implicitHeight : 0 }
        StackLayout {
            id: pages
            objectName: "studioPageStack"
            Layout.fillWidth: true; Layout.fillHeight: true
            DashboardPage { objectName: "studioDashboard"; onNavigate: (index) => window.navigatePage(index) }
            ThemePage { objectName: "featureTheme" }
            ResourcesPage { objectName: "featureResources" }
            FpsPage { objectName: "featureFps" }
            IntegrityPage { objectName: "featureIntegrity" }
            BootVideoPage { objectName: "featureBoot" }
            UiPage { objectName: "featureInterface"; onNotify: (msg) => toast.show(msg) }
            RpcidPage { objectName: "featureRpc" }
            GifRpcPage { objectName: "featureGif" }
            WorkspacePage { objectName: "featureWorkspace"; onNavigate: (index) => window.navigatePage(index) }
            DebloatPage { objectName: "featureDebloat"; onNotify: (msg) => toast.show(msg) }
        }
        LogPanel {
            id: logPanel
            Layout.fillWidth: true
            Layout.preferredHeight: logPanel.collapsed ? 40 : Math.min(150, window.height * 0.2)
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
                Qt.callLater(function() { if (auth.authorized && !backend.busy) backend.detectInstalls(); });
            }
        }
    }

    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
    }

    SettingsPanel {
        id: settingsPanel
        objectName: "settingsPanel"
        onNotify: (message) => toast.show(message)
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
