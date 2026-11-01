import QtQuick
import QtQuick.Effects
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import BSICustomizer
import "Icons.js" as Icons

ApplicationWindow {
    onClosing: (close) => { if (backend.busy) close.accepted = false; }

    id: window
    width: 1180
    height: 760
    minimumWidth: 980
    minimumHeight: 640
    visible: true
    // Premium-Optimizer chrome: frameless window with the app's own topbar and
    // control box (startSystemMove / startSystemResize keep native snap+resize).
    flags: Qt.Window | Qt.FramelessWindowHint
    title: qsTr("BlueStacks 5 / MSI 5 — BSI Customizer")
    color: Theme.window

    // ---- probes used by the C++ --selftest (do not remove) ----
    property color testProbe: Theme.window
    property color testProbeAccent: Theme.accent

    property bool booted: false

    // ------------------------------------------------------------------
    // Painted background — the FalcoForge .bg layer: gradient field,
    // drifting orbs and a faint grid. No image is needed to start the app.
    // ------------------------------------------------------------------
    Item {
        anchors.fill: parent

        Rectangle {
            anchors.fill: parent
            color: Theme.window
        }
        Image {
            source: "img/orb-cyan.png"
            width: 420; height: 420
            x: parent.width - width + 80
            y: -120
            opacity: 0.55
            smooth: true
        }
        Image {
            source: "img/orb-blue.png"
            width: 360; height: 360
            x: -60
            y: parent.height - height + 120
            opacity: 0.55
            smooth: true
        }
        Image {
            source: "img/orb-green.png"
            width: 260; height: 260
            x: parent.width * 0.56
            y: parent.height * 0.42
            opacity: 0.5
            smooth: true
        }
        Image {
            source: "img/orb-violet.png"
            width: 220; height: 220
            x: parent.width * 0.88 - width
            y: parent.height * 0.62
            opacity: 0.5
            smooth: true
        }
        Canvas {
            id: gridLines
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);
                ctx.strokeStyle = "rgba(64, 200, 240, 0.045)";
                ctx.lineWidth = 1;
                for (let x = 0; x < width; x += 44) {
                    ctx.beginPath();
                    ctx.moveTo(x + 0.5, 0);
                    ctx.lineTo(x + 0.5, height);
                    ctx.stroke();
                }
                for (let y = 0; y < height; y += 44) {
                    ctx.beginPath();
                    ctx.moveTo(0, y + 0.5);
                    ctx.lineTo(width, y + 0.5);
                    ctx.stroke();
                }
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            Component.onCompleted: requestPaint()
        }
    }

    // ------------------------------------------------------------------
    // Topbar — FalcoForge .topbar: brand left, status dot + plan pill +
    // the window control box at the far edge. The whole strip drags the
    // frameless window.
    // ------------------------------------------------------------------
    Rectangle {
        id: topbar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 48
        gradient: Gradient {
            GradientStop { position: 0; color: "#061120" }
            GradientStop { position: 1; color: "#030a14" }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.neonFaint
        }

        RowLayout {
            anchors.fill: parent
            anchors.rightMargin: 0
            spacing: 0

            Row {
                Layout.leftMargin: 14
                spacing: 10

                Rectangle {
                    width: 32; height: 32; radius: 8
                    anchors.verticalCenter: parent.verticalCenter
                    color: "#0a1a2c"
                    border.width: 1
                    border.color: Theme.neonFaint
                    clip: true

                    Image {
                        anchors.fill: parent
                        anchors.margins: 2
                        source: "img/app-icon.png"
                        sourceSize: Qt.size(28, 28)
                        smooth: true
                    }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: qsTr("BSI Customizer")
                        color: Theme.text
                        font.pixelSize: 14
                        font.weight: Font.Bold
                    }
                    Text {
                        text: qsTr("BlueStacks 5 / MSI 5 toolkit — dev build")
                        color: Theme.textDim
                        font.pixelSize: 11
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                id: titleDot
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                radius: 4
                color: auth.authorized ? "#4dffd9" : "#ffc45c"
            }

            Rectangle {
                Layout.leftMargin: 10
                Layout.preferredHeight: 26
                Layout.preferredWidth: planLabel.implicitWidth + 24
                radius: 13
                color: "transparent"
                border.width: 1
                border.color: auth.authorized ? Qt.rgba(77 / 255, 255 / 255, 217 / 255, 0.5)
                                              : Theme.neonFaint

                Text {
                    id: planLabel
                    anchors.centerIn: parent
                    text: auth.authorized ? qsTr("Dev access") : qsTr("Locked")
                    color: auth.authorized ? Theme.green : Theme.textDim
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
            }

            Rectangle {
                id: settingsButton
                Layout.leftMargin: 10
                Layout.rightMargin: 8
                Layout.preferredWidth: 92
                Layout.preferredHeight: 30
                radius: 9
                color: settingsHover.containsMouse ? Theme.raised : "transparent"
                border.width: 1
                border.color: settingsHover.containsMouse ? Theme.borderStrong : Theme.border
                Row {
                    anchors.centerIn: parent
                    spacing: 7
                    Image {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 15
                        height: 15
                        source: Icons.icon(Icons.settings, settingsHover.containsMouse ? Theme.accentText : Theme.textDim)
                        smooth: true
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Settings")
                        color: settingsHover.containsMouse ? Theme.accentText : Theme.soft
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }
                MouseArea {
                    id: settingsHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: settingsPanel.open()
                }
                ToolTip.visible: settingsHover.containsMouse
                ToolTip.text: qsTr("Open appearance and About settings")
                ToolTip.delay: 500
            }

            WinControls {
                Layout.leftMargin: 0
                appWindow: window
            }
        }

        // Drag the frameless window from the strip; double-click toggles max.
        MouseArea {
            anchors.fill: parent
            anchors.rightMargin: 260
            property real lastClick: 0
            onPressed: {
                const now = Date.now();
                if (now - lastClick < 320) {
                    if (window.visibility === Window.Maximized)
                        window.showNormal();
                    else
                        window.showMaximized();
                }
                lastClick = now;
                window.startSystemMove();
            }
        }
    }

    // ------------------------------------------------------------------
    // Sidebar — grouped the way the toolbox groups its menu, with the
    // signed-in account pinned at the bottom.
    // ------------------------------------------------------------------
    RowLayout {
        id: shell
        anchors.top: topbar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 268
            gradient: Gradient {
                GradientStop { position: 0; color: "#050e1a" }
                GradientStop { position: 1; color: "#020710" }
            }

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.neonFaint
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 3

                Item { Layout.preferredHeight: 10 }

                Repeater {
                    model: [
                        { header: true,  label: qsTr("CUSTOMIZE") },
                        { page: 0, label: qsTr("Dashboard"),   icon: Icons.dashboard },
                        { page: 1, label: qsTr("Theme"),       icon: Icons.theme },
                        { page: 2, label: qsTr("Resources"),   icon: Icons.images },
                        { header: true,  label: qsTr("TUNE") },
                        { page: 3, label: qsTr("FPS"),         icon: Icons.fps },
                        { page: 4, label: qsTr("Integrity"),   icon: Icons.integrity },
                        { page: 5, label: qsTr("Boot Video"),  icon: Icons.boot },
                        { header: true,  label: qsTr("TOOLS") },
                        { page: 6, label: qsTr("UI"),          icon: Icons.extras },
                        { page: 7, label: qsTr("Debloat"),     icon: Icons.debloat },
                        { page: 8, label: qsTr("Discord RPC"), icon: Icons.discord }
                    ]
                    delegate: Loader {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: modelData.header ? 30 : 42
                        sourceComponent: modelData.header ? navHeader : navButton

                        onLoaded: {
                            if (modelData.header) {
                                item.labelText = modelData.label;
                            } else {
                                item.label = modelData.label;
                                item.iconPath = modelData.icon;
                                item.active = Qt.binding(() => pages.currentIndex === modelData.page);
                                item.clicked.connect(() => pages.currentIndex = modelData.page);
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                // ---- the signed-in account, pinned at the bottom ----
                Rectangle {
                    visible: auth.authorized
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    Layout.bottomMargin: 6
                    radius: 12
                    color: Qt.rgba(1, 1, 1, 0.02)
                    border.width: 1
                    border.color: Theme.neonFaint

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        Rectangle {
                            Layout.preferredWidth: 38
                            Layout.preferredHeight: 38
                            radius: width / 2
                            color: "#0a1a2c"
                            border.width: 1
                            border.color: Qt.rgba(56 / 255, 225 / 255, 255 / 255, 0.5)

                            Text {
                                anchors.centerIn: parent
                                visible: accountAvatar.status !== Image.Ready
                                text: {
                                    const name = auth.user.DisplayName || auth.user.Username || "?";
                                    return name.charAt(0).toUpperCase();
                                }
                                color: Theme.neon
                                font.pixelSize: 14
                                font.weight: Font.Bold
                            }
                            Image {
                                id: accountAvatar
                                anchors.fill: parent
                                anchors.margins: 1
                                source: auth.user.AvatarUrl || ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                smooth: true
                                layer.enabled: true
                                layer.effect: MultiEffect {
                                    maskEnabled: true
                                    maskSource: accountAvatarMask
                                }
                            }
                            Rectangle {
                                id: accountAvatarMask
                                width: accountAvatar.width
                                height: accountAvatar.height
                                radius: width / 2
                                color: "white"
                                antialiasing: true
                                visible: false
                                layer.enabled: true
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                Layout.fillWidth: true
                                text: auth.user.DisplayName || auth.user.Username || qsTr("Signed in")
                                color: Theme.text
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                text: qsTr("Dev access")
                                color: Theme.green
                                font.pixelSize: 11
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: 62
                            Layout.preferredHeight: 28
                            radius: 8
                            color: signOutArea.containsMouse ? Qt.rgba(1, 111 / 255, 131 / 255, 0.12) : "transparent"
                            border.width: 1
                            border.color: Theme.border

                            Text {
                                anchors.centerIn: parent
                                text: qsTr("Sign out")
                                color: signOutArea.containsMouse ? Theme.danger : Theme.textDim
                                font.pixelSize: 11
                            }
                            MouseArea {
                                id: signOutArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: auth.signOut()
                            }
                        }
                    }
                }
            }
        }

        // ---------------- content ----------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Self-update strip: only takes space while an update is being
            // offered, downloaded or installed (see UpdateBanner.qml).
            UpdateBanner {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.topMargin: visible ? 10 : 0
                Layout.preferredHeight: visible ? implicitHeight : 0
            }

            StackLayout {
                id: pages
                Layout.fillWidth: true
                Layout.fillHeight: true

                DashboardPage {}
                ThemePage {}
                ResourcesPage {}
                FpsPage {}
                IntegrityPage {}
                BootVideoPage {}
                UiPage {
                    onNotify: (msg) => toast.show(msg)
                }
                DebloatPage {
                    onNotify: (msg) => toast.show(msg)
                }
                RpcidPage {}
            }

            LogPanel {
                id: logPanel
                Layout.fillWidth: true
                Layout.preferredHeight: logPanel.collapsed ? 42 : 150
                Behavior on Layout.preferredHeight { NumberAnimation { duration: 140 } }
            }
        }
    }

    SettingsPanel {
        id: settingsPanel
        onNotify: (message) => toast.show(message)
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
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.textDim
                font.pixelSize: 11
                font.weight: Font.Bold
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
            color: "#0a1a2c"
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
