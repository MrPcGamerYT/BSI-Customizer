import QtQuick
import QtQuick.Effects
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import BSICustomizer

// The studio sign-in surface retains the existing Discord resume/manual flows.
// AuthManager, not this visual gate, remains the authorization authority.
Item {
    id: gate

    // The ApplicationWindow, for drag / minimize / maximize / close.
    property var appWindow

    // Local card state (FalcoForge's gate.page/gate.mode equivalents).
    property bool hideResume: false
    property bool manualMode: false

    function startDiscord() {
        const begin = auth.startDiscord();
        gate.manualMode = begin.Manual === true;
    }

    function finishManual() {
        const link = callbackInput.text.trim();
        if (link.length === 0) {
            callbackInput.forceActiveFocus();
            return;
        }
        auth.completeManual(link);
    }

    Rectangle { anchors.fill: parent; color: Theme.window }
    // Same mint glass atmosphere as the supplied workspace, without moving
    // OAuth or the saved-session flow into a browser renderer.
    Image {
        anchors.left: parent.left; anchors.top: parent.top
        width: parent.width * 0.65; height: parent.height * 0.9
        source: "img/orb-green.png"; opacity: Theme.dark ? 0.26 : 0.08
        fillMode: Image.PreserveAspectFit
    }
    Image {
        anchors.right: parent.right; anchors.bottom: parent.bottom
        width: parent.width * 0.6; height: parent.height * 0.8
        source: "img/orb-green.png"; opacity: Theme.dark ? 0.16 : 0.06
        fillMode: Image.PreserveAspectFit
    }

    // ---- the window row (FalcoForge .login-window-row) ----
    Rectangle {
        id: windowRow
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 48
        color: Theme.topbar
        border.width: 1
        border.color: Theme.neonFaint

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.neonFaint
        }

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            Rectangle {
                width: 8; height: 8; radius: 4
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.neon
                opacity: 0.9
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("BSI CUSTOMIZER")
                color: Theme.soft
                font.pixelSize: 11
                font.weight: Font.Bold
                font.letterSpacing: 1.3
            }
        }

        WinControls {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            appWindow: gate.appWindow
        }

        // Drag the frameless window from the strip.
        MouseArea {
            anchors.fill: parent
            anchors.rightMargin: 146  // keep the control box clickable
            property real lastClick: 0
            onPressed: {
                const now = Date.now();
                if (now - gate.lastWindowClick < 320 && gate.appWindow) {
                    if (gate.appWindow.visibility === Window.Maximized)
                        gate.appWindow.showNormal();
                    else
                        gate.appWindow.showMaximized();
                }
                gate.lastWindowClick = now;
                if (gate.appWindow)
                    gate.appWindow.startSystemMove();
            }
        }
    }
    property real lastWindowClick: 0

    // ---- the login card (FalcoForge .login-card) ----
    Item {
        anchors.top: windowRow.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        Rectangle {
            id: card
            anchors.centerIn: parent
            width: Math.min(480, gate.width - 44)
            height: cardColumn.implicitHeight + 44
            radius: 20
            border.width: 1
            border.color: Theme.border
            color: Theme.card

            ColumnLayout {
                id: cardColumn
                anchors.fill: parent
                anchors.margins: 28
                spacing: 0

                // ---- head: app mark + heading ----
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14

                    Rectangle {
                        Layout.preferredWidth: 58
                        Layout.preferredHeight: 58
                        radius: 14
                        color: Theme.raised
                        border.width: 1
                        border.color: Theme.neonFaint

                        Image {
                            anchors.centerIn: parent
                            width: 46; height: 46
                            source: "img/app-icon.png"
                            sourceSize: Qt.size(46, 46)
                            smooth: true
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Make it yours.")
                            color: Theme.text
                            font.pixelSize: 17
                            font.weight: Font.Bold
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Sign in with Discord to open your BSI workspace.")
                            color: Theme.textDim
                            font.pixelSize: 13
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Item { Layout.preferredHeight: 18 }

                // ---- welcome back (one-click continue, never automatic) ----
                Rectangle {
                    id: resume
                    visible: auth.hasSavedSession && !gate.hideResume
                    Layout.fillWidth: true
                    implicitHeight: resumeColumn.implicitHeight + 32
                    radius: 16
                    border.width: 1
                    border.color: Theme.border
                    color: Theme.cardLow

                    ColumnLayout {
                        id: resumeColumn
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Rectangle {
                                width: 32; height: 32; radius: 10
                                color: Theme.accentSoft
                                border.width: 1
                                border.color: Qt.rgba(61 / 255, 240 / 255, 192 / 255, 0.3)

                                Text {
                                    anchors.centerIn: parent
                                    text: "\uD83D\uDC4B" // 👋
                                    font.pixelSize: 15
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    text: qsTr("Welcome back!")
                                    color: Theme.text
                                    font.pixelSize: 15
                                    font.weight: Font.ExtraBold
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("We found your previous sign-in on this PC")
                                    color: Theme.textDim
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        Item { Layout.preferredHeight: 14 }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Rectangle {
                                Layout.preferredWidth: 48
                                Layout.preferredHeight: 48
                                radius: width / 2
                                color: Theme.raised
                                border.width: 2
                                border.color: Qt.rgba(61 / 255, 240 / 255, 192 / 255, 0.45)

                                Text {
                                    anchors.centerIn: parent
                                    visible: resumeAvatar.status !== Image.Ready
                                    text: {
                                        const name = auth.savedUser.DisplayName
                                                || auth.savedUser.Username || "?";
                                        return name.charAt(0).toUpperCase();
                                    }
                                    color: Theme.accentText
                                    font.pixelSize: 18
                                    font.weight: Font.Bold
                                }
                                Image {
                                    id: resumeAvatar
                                    anchors.fill: parent
                                    anchors.margins: 2
                                    source: auth.savedUser.AvatarUrl || ""
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    smooth: true
                                    layer.enabled: true
                                    layer.effect: MultiEffect {
                                        maskEnabled: true
                                        maskSource: resumeAvatarMask
                                    }
                                }
                                Rectangle {
                                    id: resumeAvatarMask
                                    width: resumeAvatar.width
                                    height: resumeAvatar.height
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
                                    text: auth.savedUser.DisplayName || auth.savedUser.Username || qsTr("Signed in")
                                    color: Theme.text
                                    font.pixelSize: 15
                                    font.weight: Font.ExtraBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: auth.savedMeta
                                    color: Theme.textDim
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }
                                Rectangle {
                                    Layout.topMargin: 6
                                    Layout.preferredHeight: 20
                                    Layout.preferredWidth: methodLabel.implicitWidth + 16
                                    radius: 6
                                    color: Qt.rgba(88 / 255, 101 / 255, 242 / 255, 0.14)
                                    border.width: 1
                                    border.color: Qt.rgba(88 / 255, 101 / 255, 242 / 255, 0.28)

                                    Text {
                                        id: methodLabel
                                        anchors.centerIn: parent
                                        text: qsTr("Discord")
                                        color: Theme.purple
                                        font.pixelSize: 11
                                        font.letterSpacing: 0.2
                                    }
                                }
                            }
                        }

                        Item { Layout.preferredHeight: 14 }

                        // Continue using the existing verified-session flow.
                        Rectangle {
                            id: resumeGo
                            Layout.fillWidth: true
                            Layout.preferredHeight: 46
                            radius: 12
                            color: Theme.accent

                            Row {
                                anchors.centerIn: parent
                                spacing: 10

                                Rectangle {
                                    width: 22; height: 22; radius: 7
                                    color: Qt.rgba(0, 0, 0, 0.18)
                                    anchors.verticalCenter: parent.verticalCenter

                                    Text {
                                        anchors.centerIn: parent
                                        text: "\u2192"
                                        color: Theme.primaryText
                                        font.pixelSize: 14
                                        font.weight: Font.Black
                                    }
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: {
                                        const name = auth.savedUser.DisplayName
                                                || auth.savedUser.Username || "";
                                        return name.length > 24
                                                ? qsTr("Continue as %1…").arg(name.slice(0, 24))
                                                : qsTr("Continue as %1").arg(name);
                                    }
                                    color: Theme.primaryText
                                    font.pixelSize: 15
                                    font.weight: Font.ExtraBold
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                enabled: !auth.busy
                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: auth.continueSaved()
                            }
                        }

                        Item { Layout.preferredHeight: 10 }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 34
                            radius: 10
                            color: switchArea.containsMouse ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
                            border.width: 1
                            border.color: switchArea.containsMouse ? Qt.rgba(1, 1, 1, 0.1)
                                                                  : Qt.rgba(1, 1, 1, 0.06)

                            Text {
                                anchors.centerIn: parent
                                text: qsTr("Use a different account")
                                color: switchArea.containsMouse ? Theme.soft
                                     : Qt.rgba(147 / 255, 164 / 255, 189 / 255, 0.75)
                                font.pixelSize: 12
                            }
                            MouseArea {
                                id: switchArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    gate.hideResume = true;
                                    auth.forgetSaved();
                                }
                            }
                        }
                    }
                }

                Item {
                    visible: resume.visible
                    Layout.preferredHeight: 18
                }

                // ---- gate options: Discord is the only door ----
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Rectangle {
                        id: discordOption
                        Layout.fillWidth: true
                        Layout.preferredHeight: 66
                        radius: 10
                        color: discordArea.containsMouse && !auth.busy
                               ? Qt.rgba(56 / 255, 225 / 255, 255 / 255, 0.05)
                               : Qt.rgba(1, 1, 1, 0.015)
                        border.width: 1
                        border.color: discordArea.containsMouse && !auth.busy
                                      ? Theme.neonSoft
                                      : Qt.rgba(88 / 255, 101 / 255, 242 / 255, 0.62)
                        opacity: auth.busy ? 0.75 : 1.0

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 12

                            Rectangle {
                                Layout.preferredWidth: 42
                                Layout.preferredHeight: 42
                                radius: 10
                                color: Qt.rgba(88 / 255, 101 / 255, 242 / 255, 0.18)
                                border.width: 1
                                border.color: Qt.rgba(88 / 255, 101 / 255, 242 / 255, 0.62)

                                Image {
                                    anchors.centerIn: parent
                                    width: 22; height: 17
                                    source: "img/discord-mark.svg"
                                    sourceSize: Qt.size(44, 34)
                                    smooth: true
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text {
                                    Layout.fillWidth: true
                                    text: auth.busy ? qsTr("Waiting for Discord...")
                                                    : qsTr("Continue with Discord")
                                    color: Theme.text
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("Secure browser sign-in · Discord access rules apply")
                                    color: Theme.textDim
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                            }

                            Text {
                                text: "\u203A"
                                color: Theme.neon
                                font.pixelSize: 17
                            }
                        }

                        MouseArea {
                            id: discordArea
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: !auth.busy
                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: gate.startDiscord()
                        }
                    }
                }

                // ---- progress bar (FalcoForge .login-bar) ----
                Rectangle {
                    id: barTrack
                    Layout.fillWidth: true
                    Layout.topMargin: 16
                    Layout.preferredHeight: 4
                    radius: 2
                    color: Qt.rgba(1, 1, 1, 0.06)
                    clip: true

                    Rectangle {
                        id: barFill
                        height: parent.height
                        radius: 2
                        color: Theme.accent
                        width: auth.progressIndeterminate
                               ? barTrack.width * 0.3
                               : Math.max(barTrack.width * auth.progress, auth.progress > 0 ? 12 : 0)
                        x: auth.progressIndeterminate ? slide.offset : 0
                        visible: auth.progressIndeterminate || auth.progress > 0
                    }
                }

                Item { Layout.preferredHeight: 12 }

                // ---- note line ----
                Text {
                    Layout.fillWidth: true
                    text: auth.note
                    color: auth.noteKind === "bad" ? Theme.danger
                         : auth.noteKind === "good" ? Theme.accentText
                         : Theme.textDim
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    Layout.minimumHeight: 34
                }

                // ---- paste-the-link fallback (listener unavailable) ----
                Rectangle {
                    visible: gate.manualMode
                    Layout.fillWidth: true
                    implicitHeight: manualColumn.implicitHeight + 24
                    radius: 8
                    color: Theme.cardLow
                    border.width: 1
                    border.color: Qt.rgba(23 / 255, 126 / 255, 207 / 255, 0.55)

                    ColumnLayout {
                        id: manualColumn
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6

                        Text {
                            text: qsTr("Discord authorization URL or code")
                            color: Theme.textDim
                            font.pixelSize: 12
                            font.letterSpacing: 0.4
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            TextField {
                                id: callbackInput
                                Layout.fillWidth: true
                                placeholderText: qsTr("Paste the address or code here")
                                color: Theme.text
                                placeholderTextColor: Theme.textDim
                                font.pixelSize: 13
                                selectByMouse: true
                                background: Rectangle {
                                    radius: 8
                                    color: Theme.cardLow
                                    border.width: 1
                                    border.color: callbackInput.activeFocus ? Theme.neonSoft
                                                                           : Qt.rgba(56 / 255, 225 / 255, 255 / 255, 0.2)
                                }
                                onAccepted: gate.finishManual()
                            }
                            Rectangle {
                                Layout.preferredWidth: 72
                                Layout.preferredHeight: 40
                                radius: 8
                                color: Theme.accent
                                Text {
                                    anchors.centerIn: parent
                                    text: qsTr("Finish")
                                    color: Theme.primaryText
                                    font.pixelSize: 13
                                    font.weight: Font.Bold
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: gate.finishManual()
                                }
                            }
                        }
                    }
                }

                // ---- foot ----
                Text {
                    visible: auth.inviteUrl.length > 0
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    text: "Need access? <a href=\"" + auth.inviteUrl + "\">Join the Discord server</a>"
                    textFormat: Text.StyledText
                    color: Theme.textDim
                    linkColor: Theme.neonSoft
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    onLinkActivated: Qt.openUrlExternally(link)
                }
                Text {
                    visible: auth.setupHint.length > 0 && auth.setupHint !== auth.note
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    text: auth.setupHint
                    color: Theme.textDim
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    opacity: 0.85
                }
            }
        }
    }

    // The indeterminate bar slide (FalcoForge's loadingSlide).
    QtObject {
        id: slide
        property real offset: 0
    }
    NumberAnimation {
        id: barSlide
        target: slide
        property: "offset"
        from: barTrack ? -barTrack.width * 0.3 : -120
        to: barTrack ? barTrack.width : 300
        duration: 1150
        loops: Animation.Infinite
        running: auth.progressIndeterminate && gate.visible
    }
}
