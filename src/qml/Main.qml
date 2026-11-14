import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import BSICustomizer
import "Icons.js" as Icons
import "WindowGeometry.js" as WindowGeometry

ApplicationWindow {
    onClosing: (close) => { if (backend.busy) close.accepted = false; }

    id: window
    width: 1420
    height: 920
    minimumWidth: 980
    minimumHeight: 640
    function fitToAvailableScreen() {
        const bounds = { x: Screen.virtualX, y: Screen.virtualY,
                         width: Screen.desktopAvailableWidth, height: Screen.desktopAvailableHeight };
        const fitted = WindowGeometry.initialGeometry(bounds);
        minimumWidth = fitted.minimumWidth;
        minimumHeight = fitted.minimumHeight;
        width = fitted.width;
        height = fitted.height;
        x = fitted.x;
        y = fitted.y;
    }

    Component.onCompleted: fitToAvailableScreen()

    function runSelftest() {
        // This is a shell check only; native engine tests live in --selftest.
        const ok = typeof window.navigatePage === "function"
                   && pages.count === window.featurePages.length;
        toast.show(ok ? qsTr("Shell navigation check passed. Run --selftest for engine tests.")
                      : qsTr("Shell navigation check failed."));
    }
    visible: true
    // Premium-Optimizer chrome: frameless window with the app's own topbar and
    // control box (startSystemMove / startSystemResize keep native snap+resize).
    flags: Qt.Window | Qt.FramelessWindowHint
    title: qsTr("BlueStacks 5 / MSI 5 — BSI Customizer")
    color: Theme.window
    font.family: Theme.bodyFont
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
        { label: qsTr("Overview"), section: qsTr("HOME"), icon: Icons.dashboard },
        { label: qsTr("Color studio"), section: qsTr("PATCHER"), icon: Icons.theme },
        { label: qsTr("Resources"), section: qsTr("PATCHER"), icon: Icons.images },
        { label: qsTr("Performance"), section: qsTr("PATCHER"), icon: Icons.fps },
        { label: qsTr("Integrity & safety"), section: qsTr("PATCHER"), icon: Icons.integrity },
        { label: qsTr("Boot experience"), section: qsTr("PATCHER"), icon: Icons.boot },
        { label: qsTr("Player interface"), section: qsTr("PATCHER"), icon: Icons.extras },
        { label: qsTr("Discord presence"), section: qsTr("PATCHER"), icon: Icons.discord },
        { label: qsTr("Animated presence"), section: qsTr("PATCHER"), icon: Icons.discord },
        { label: qsTr("Patch workspace"), section: qsTr("PATCHER"), icon: Icons.extras },
        { label: qsTr("Debloat & disk manager"), section: qsTr("PATCHER"), icon: Icons.debloat },
        { label: qsTr("Backups"), section: qsTr("RECOVERY"), icon: Icons.integrity },
        { label: qsTr("Add-ons"), section: qsTr("COLLECTION"), icon: Icons.theme }
    ]
    readonly property var pageNames: featurePages.map(p => p.label)
    property alias currentPage: pages.currentIndex
    function navigatePage(index) {
        if (Number.isInteger(index) && index >= 0 && index < featurePages.length)
            pages.currentIndex = index;
    }

    ControlCenterBackdrop {
        anchors.fill: parent
        animationAllowed: window.visible && window.active && window.visibility !== Window.Minimized
    }

    Rectangle {
        id: rail; objectName: "studioSidebar"
        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
        width: 200; color: "transparent"; visible: auth.authorized
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }
        MicroLabel { x: 26; y: 37; text: "BSI / 4.0.1"; color: Theme.tertiary; font.pixelSize: 9 }
        ScrollView {
            id: navScroll
            anchors.top: parent.top; anchors.topMargin: 105; anchors.bottom: railFooter.top; anchors.bottomMargin: 20
            anchors.left: parent.left; anchors.right: parent.right
            contentWidth: availableWidth; clip: true; ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            Column {
                width: navScroll.availableWidth; spacing: 7
                Repeater {
                    model: [
                        {label:"HOME",icon:Icons.dashboard,route:0,children:[]},
                        {label:"PATCHER",icon:Icons.theme,route:1,children:[1,2,3,4,5,6]},
                        {label:"PRESENCE",icon:Icons.discord,route:7,children:[7,8]},
                        {label:"APP",icon:Icons.settings,route:-1,children:[]},
                        {label:"TOOLS",icon:Icons.debloat,route:9,children:[9,10]},
                        {label:"+",icon:"<path d='M12 5v14M5 12h14'/>",route:0,children:[]},
                        {label:"RECOVERY",icon:Icons.integrity,route:11,children:[]},
                        {label:"COLLECTION",icon:Icons.images,route:12,children:[]}
                    ]
                    delegate: Column {
                        id: navGroup
                        required property var modelData
                        required property int index
                        property bool expanded: false
                        readonly property bool selected: pages.currentIndex === modelData.route || modelData.children.indexOf(pages.currentIndex)>=0
                        width: parent.width; spacing: 3
                        AbstractButton {
                            id: navButton; objectName: "navGroup" + navGroup.index
                            width: parent.width; height: 42; Accessible.name: navGroup.modelData.label
                            background: Rectangle {
                                radius: 10
                                color: navButton.hovered ? Theme.hoverFill : "transparent"
                                Behavior on color { ColorAnimation { duration: Theme.motionDuration } }
                                Rectangle { visible: navGroup.selected; x: 0; width: 2; height: 18; radius: 1; anchors.verticalCenter: parent.verticalCenter; color: Theme.accent }
                            }
                            contentItem: Item {
                                Image { x: 26; anchors.verticalCenter: parent.verticalCenter; width: 16; height: 16; source: Icons.icon(navGroup.modelData.icon,navGroup.selected ? Theme.text : Theme.tertiary); sourceSize: Qt.size(16,16) }
                                MicroLabel { x: 54; anchors.verticalCenter: parent.verticalCenter; text: navGroup.modelData.label; color: navGroup.selected ? Theme.text : Theme.textDim; font.pixelSize: 10 }
                                Rectangle { visible: navGroup.modelData.route===11; width: 4; height: 4; radius: 2; x: 158; anchors.verticalCenter: parent.verticalCenter; color: Theme.accent }
                                Text { visible: navGroup.modelData.route===12 || navGroup.modelData.children.length>0; anchors.right: parent.right; anchors.rightMargin: 19; anchors.verticalCenter: parent.verticalCenter; text: navGroup.selected || navGroup.expanded ? "⌄" : "›"; color: Theme.tertiary; font.pixelSize: 12 }
                            }
                            onClicked: {
                                if(navGroup.modelData.route===-1) { settingsPanel.currentTab=0; settingsPanel.open(); }
                                else if(navGroup.modelData.children.length>0) { navGroup.expanded=!navGroup.expanded; if(!navGroup.selected) window.navigatePage(navGroup.modelData.route); }
                                else window.navigatePage(navGroup.modelData.route);
                            }
                        }
                        Column {
                            width: parent.width; visible: height > 0; spacing: 2; clip: true
                            height: navGroup.expanded ? implicitHeight : 0
                            opacity: navGroup.expanded ? 1 : 0
                            enabled: navGroup.expanded
                            Behavior on height { NumberAnimation { duration: Theme.revealDuration; easing.type: Easing.OutCubic } }
                            Behavior on opacity { NumberAnimation { duration: Theme.motionDuration } }
                            Repeater {
                                model: navGroup.modelData.children
                                AbstractButton {
                                    id: subButton; required property int modelData
                                    objectName: "featureNav" + modelData
                                    width: parent.width; height: 33
                                    background: Rectangle {
                                        radius: 8; color: subButton.hovered ? Theme.hoverFill : "transparent"
                                        Behavior on color { ColorAnimation { duration: Theme.motionDuration } }
                                    }
                                    contentItem: Text { leftPadding: 54; verticalAlignment: Text.AlignVCenter; text: window.featurePages[subButton.modelData].label; color: pages.currentIndex===subButton.modelData ? Theme.text : Theme.textDim; font.family: Theme.bodyFont; font.pixelSize: 11; elide: Text.ElideRight }
                                    onClicked: window.navigatePage(modelData)
                                }
                            }
                        }
                    }
                }
            }
        }
        ColumnLayout {
            id: railFooter; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 20; spacing: 15
            MicroLabel { text: qsTr("ACTIVE TARGET"); font.pixelSize: 8; color: Theme.tertiary }
            ComboBox {
                id: targetPicker; objectName: "shellTargetPicker"; Layout.fillWidth: true; implicitHeight: 30; font.pixelSize: 10
                enabled: !backend.busy; model: backend.installs; textRole: "profile"
                currentIndex: backend.installs.findIndex(i => i.exe === backend.exePath)
                displayText: currentText || qsTr("Select target on Home")
                onActivated: backend.useInstall(currentIndex)
            }
            RowLayout {
                Layout.fillWidth: true
                CircleButton { id: settingsButton; objectName: "settingsButton"; text: qsTr("Open settings"); glyph: Icons.settings; onClicked: { settingsPanel.currentTab=0; settingsPanel.open(); } }
                Item { Layout.fillWidth: true }
                CircleButton { text: qsTr("Application menu"); glyph: "<circle cx='5' cy='12' r='1'/><circle cx='12' cy='12' r='1'/><circle cx='19' cy='12' r='1'/>"; onClicked: appMenu.popup() }
            }
        }
    }
    Item {
        id: topbar
        anchors.left: rail.right; anchors.right: parent.right; anchors.top: parent.top
        anchors.leftMargin: 32; anchors.rightMargin: 32; height: 104; visible: auth.authorized
        MouseArea { anchors.fill: parent; onPressed: window.startSystemMove(); onDoubleClicked: window.visibility===Window.Maximized ? window.showNormal() : window.showMaximized() }
        RowLayout {
            anchors.fill: parent; spacing: 12
            Rectangle { Layout.preferredWidth: 33; Layout.preferredHeight: 29; radius: 7; color: "transparent"; border.color: Theme.border
                MicroLabel { anchors.centerIn: parent; text: "BSI"; color: Theme.textDim; font.pixelSize: 9; font.letterSpacing: .5 }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 5
                Text { text: qsTr("BSI Customizer"); color: Theme.text; font.family: Theme.headingFont; font.pixelSize: 17; font.weight: Font.DemiBold; font.letterSpacing: -.4 }
                Text { text: qsTr("Your player, thoughtfully customized"); color: Theme.tertiary; font.pixelSize: 11 }
            }
            Rectangle {
                Layout.preferredWidth: 78; Layout.preferredHeight: 28; radius: 14
                color: backend.busy ? Theme.raised : "#0cc8e86a"; border.color: backend.busy ? Theme.border : "#24c8e86a"
                Row { anchors.centerIn: parent; spacing: 6
                    Rectangle { width: 4; height: 4; radius: 2; color: backend.busy ? Theme.textDim : Theme.ready; anchors.verticalCenter: parent.verticalCenter }
                    MicroLabel { text: backend.busy ? qsTr("BUSY") : qsTr("READY"); font.pixelSize: 8; font.letterSpacing: .8; color: backend.busy ? Theme.textDim : Theme.ready }
                }
            }
            CircleButton { text: qsTr("Preview collection"); onClicked: { window.navigatePage(12); collectionPage.preview(); } }
            CircleButton { text: qsTr("Close window"); glyph: "<path d='m7 7 10 10M7 17 17 7'/>"; onClicked: window.close() }
        }
    }
    Menu {
        id: appMenu
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motionDuration } }
        exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.motionDuration } }
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
        MenuItem {
            text: qsTr("Selftest")
            enabled: !backend.busy
            onTriggered: runSelftest()
        }
        MenuItem { text: qsTr("Animated background"); checkable: true; checked: Theme.backgroundMotion; enabled: !Theme.reduceMotion; onTriggered: themes.backgroundMotion = checked }
        MenuItem { text: qsTr("Reduce motion"); checkable: true; checked: Theme.reduceMotion; onTriggered: themes.reduceMotion = checked }
        MenuSeparator {}
        MenuItem { text: qsTr("Sign out"); enabled: !backend.busy; onTriggered: auth.signOut() }
    }

    LoadingStrip {
        objectName: "shellLoadingStrip"
        anchors.left: rail.right; anchors.right: parent.right; anchors.top: topbar.bottom
        z: 10
        busy: auth.authorized && (backend.busy ||
              (pages.children[pages.currentIndex] && pages.children[pages.currentIndex].status === Loader.Loading))
        animationAllowed: window.active && window.visibility !== Window.Minimized
    }

    ColumnLayout {
        id: shell
        anchors.top: topbar.bottom; anchors.bottom: parent.bottom
        anchors.left: rail.right; anchors.right: parent.right
        anchors.leftMargin: 32; anchors.rightMargin: 32; anchors.bottomMargin: 22
        visible: auth.authorized
        spacing: 10
        UpdateBanner { Layout.fillWidth: true; Layout.preferredHeight: visible ? implicitHeight : 0 }
        StackLayout {
            id: pages
            currentIndex: 12
            objectName: "studioPageStack"
            Layout.fillWidth: true; Layout.fillHeight: true
            LazyFeature { objectName: "studioDashboardLoader"; pageIndex: 0; currentPage: pages.currentIndex; authorized: auth.authorized; pageSource: Qt.resolvedUrl("pages/DashboardPage.qml"); onNotify: (msg) => toast.show(msg); onNavigate: (index) => window.navigatePage(index) }
            LazyFeature { objectName: "featureTheme"; pageIndex: 1; currentPage: pages.currentIndex; authorized: auth.authorized; pageSource: Qt.resolvedUrl("pages/ThemePage.qml"); onNotify: (msg) => toast.show(msg); onNavigate: (index) => window.navigatePage(index) }
            LazyFeature { objectName: "featureResources"; pageIndex: 2; currentPage: pages.currentIndex; authorized: auth.authorized; pageSource: Qt.resolvedUrl("pages/ResourcesPage.qml"); onNotify: (msg) => toast.show(msg); onNavigate: (index) => window.navigatePage(index) }
            LazyFeature { objectName: "featureFps"; pageIndex: 3; currentPage: pages.currentIndex; authorized: auth.authorized; pageSource: Qt.resolvedUrl("pages/FpsPage.qml"); onNotify: (msg) => toast.show(msg); onNavigate: (index) => window.navigatePage(index) }
            LazyFeature { objectName: "featureIntegrity"; pageIndex: 4; currentPage: pages.currentIndex; authorized: auth.authorized; pageSource: Qt.resolvedUrl("pages/IntegrityPage.qml"); onNotify: (msg) => toast.show(msg); onNavigate: (index) => window.navigatePage(index) }
            LazyFeature { objectName: "featureBoot"; pageIndex: 5; currentPage: pages.currentIndex; authorized: auth.authorized; pageSource: Qt.resolvedUrl("pages/BootVideoPage.qml"); onNotify: (msg) => toast.show(msg); onNavigate: (index) => window.navigatePage(index) }
            LazyFeature { objectName: "featureInterface"; pageIndex: 6; currentPage: pages.currentIndex; authorized: auth.authorized; pageSource: Qt.resolvedUrl("pages/UiPage.qml"); onNotify: (msg) => toast.show(msg); onNavigate: (index) => window.navigatePage(index) }
            LazyFeature { objectName: "featureRpc"; pageIndex: 7; currentPage: pages.currentIndex; authorized: auth.authorized; pageSource: Qt.resolvedUrl("pages/RpcidPage.qml"); onNotify: (msg) => toast.show(msg); onNavigate: (index) => window.navigatePage(index) }
            LazyFeature { objectName: "featureGif"; pageIndex: 8; currentPage: pages.currentIndex; authorized: auth.authorized; pageSource: Qt.resolvedUrl("pages/GifRpcPage.qml"); onNotify: (msg) => toast.show(msg); onNavigate: (index) => window.navigatePage(index) }
            LazyFeature { objectName: "featureWorkspace"; pageIndex: 9; currentPage: pages.currentIndex; authorized: auth.authorized; pageSource: Qt.resolvedUrl("pages/WorkspacePage.qml"); onNotify: (msg) => toast.show(msg); onNavigate: (index) => window.navigatePage(index) }
            LazyFeature { objectName: "featureDebloat"; pageIndex: 10; currentPage: pages.currentIndex; authorized: auth.authorized; pageSource: Qt.resolvedUrl("pages/DebloatPage.qml"); onNotify: (msg) => toast.show(msg); onNavigate: (index) => window.navigatePage(index) }
            LazyFeature { objectName: "featureBackups"; pageIndex: 11; currentPage: pages.currentIndex; authorized: auth.authorized; pageSource: Qt.resolvedUrl("pages/BackupsPage.qml"); onNotify: (msg) => toast.show(msg); onNavigate: (index) => window.navigatePage(index) }
            LazyFeature { id: collectionPage; objectName: "featureAddons"; pageIndex: 12; currentPage: pages.currentIndex; authorized: auth.authorized; pageSource: Qt.resolvedUrl("pages/AddonsPage.qml"); onNotify: (msg) => toast.show(msg); onNavigate: (index) => window.navigatePage(index) }
        }
        LogPanel {
            id: logPanel
            Layout.fillWidth: true
            Layout.preferredHeight: logPanel.collapsed ? 83 : Math.min(190, window.height * 0.24)
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
        Behavior on opacity { NumberAnimation { duration: Theme.revealDuration } }
    }

    // Installation detection is an explicit Home action, not a startup scan.

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

    // Busy is visible in the header. Engine serialization and per-action guards
    // remain authoritative; scrolling/navigation/reading never need a modal.
}
