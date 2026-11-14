import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import BSICustomizer
import "Icons.js" as Icons
import "SkinCatalog.js" as Catalog

// Presentation only: AuthManager and Backend retain all authorization checks.
Item {
    id: gate
    property var appWindow
    property bool hideResume: false
    property bool manualMode: false
    function startDiscord() { const r = auth.startDiscord(); manualMode = r.Manual === true; }
    function finishManual() { if (callbackInput.text.trim().length) auth.completeManual(callbackInput.text.trim()); }
    Rectangle { anchors.fill: parent; color: Theme.window }
    ControlCenterBackdrop { anchors.fill: parent }
    Item {
        height: 54; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        MouseArea { anchors.fill: parent; onPressed: if (gate.appWindow) gate.appWindow.startSystemMove() }
        Label { anchors.left: parent.left; anchors.leftMargin: 24; anchors.verticalCenter: parent.verticalCenter; text: "BSI Customizer / 4.0.1"; color: Theme.textDim; font.pixelSize: 12 }
        WinControls { anchors.right: parent.right; appWindow: gate.appWindow; barHeight: 48 }
    }
    Flickable {
        id: loginScroll; anchors.fill: parent; anchors.topMargin: 54; clip: true
        contentWidth: width
        contentHeight: Math.max(height, loginRow.implicitHeight + 48)
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
    RowLayout {
        id: loginRow
        x: (loginScroll.width - width) / 2
        y: Math.max(24, (loginScroll.height - height) / 2)
        width: Math.min(1040, loginScroll.width - 48)
        height: implicitHeight
        spacing: 40
        ColumnLayout {
            visible: gate.width >= 1100
            Layout.fillWidth: true; Layout.preferredWidth: 500; spacing: 20
            Label { text: "BSI / NATIVE STUDIO"; color: Theme.tertiary; font.family: Theme.monoFont; font.pixelSize: 10; font.letterSpacing: 2 }
            Label { Layout.fillWidth: true; text: qsTr("Your player.\nYour identity."); color: Theme.text; font.family: Theme.headingFont; font.pixelSize: 46; font.letterSpacing: -1.6; font.weight: Font.DemiBold; lineHeight: 1.05 }
            Label { Layout.fillWidth: true; text: qsTr("A quieter workspace. A deeper level of control.\nCreate a collection that feels like you."); color: Theme.textDim; font.pixelSize: 15; wrapMode: Text.Wrap; lineHeight: 1.5 }
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 205
                color: Theme.card; radius: 18; border.color: Theme.border
                FrameCorners { anchors.fill: parent; anchors.margins: 20; tint: Theme.tertiary }
                MiniPlayer { width: 240; height: 140; anchors.centerIn: parent; tint: "#74688d" }
            }
            Label { text: qsTr("QML + C++  •  Local tools  •  Verified recovery"); color: Theme.textDim; font.pixelSize: 11 }
        }
        Rectangle {
            id: loginCard
            Layout.preferredWidth: Math.min(430, gate.width - 72)
            Layout.preferredHeight: loginColumn.implicitHeight + 56
            Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
            color: Theme.card; radius: 22; border.color: Theme.borderStrong
            ColumnLayout {
                id: loginColumn
                anchors.fill: parent; anchors.margins: 28; spacing: 16
                Image { Layout.preferredWidth: 48; Layout.preferredHeight: 48; source: "img/app-icon.png" }
                Label { text: qsTr("Welcome to your workspace."); color: Theme.text; font.family: Theme.headingFont; font.pixelSize: 25; font.letterSpacing: -.8; font.weight: Font.DemiBold; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Label { Layout.fillWidth: true; text: qsTr("Sign in with Discord to verify your access. Your saved session is checked again before the tools unlock."); color: Theme.textDim; font.pixelSize: 13; wrapMode: Text.Wrap; lineHeight: 1.4 }
                Rectangle {
                    visible: auth.hasSavedSession && !gate.hideResume
                    Layout.fillWidth: true; implicitHeight: savedColumn.implicitHeight + 28
                    color: Theme.cardLow; radius: 12; border.color: Theme.border
                    ColumnLayout {
                        id: savedColumn; anchors.fill: parent; anchors.margins: 14; spacing: 10
                        Label { Layout.fillWidth: true; text: auth.savedUser.DisplayName || auth.savedUser.Username || qsTr("Saved Discord session"); textFormat: Text.PlainText; color: Theme.text; font.bold: true; elide: Text.ElideRight }
                        Label { Layout.fillWidth: true; text: auth.savedMeta; color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.Wrap }
                        RowLayout {
                            ActionButton { text: qsTr("Continue saved session"); accent: true; enabled: !auth.busy; onClicked: auth.continueSaved() }
                            ActionButton { text: qsTr("Forget"); enabled: !auth.busy; onClicked: { auth.forgetSaved(); gate.hideResume = true; } }
                        }
                    }
                }
                ActionButton { objectName: "nativeDiscordLogin"; Layout.fillWidth: true; implicitHeight: 44; text: auth.busy ? qsTr("Verifying Discord…") : qsTr("Continue with Discord →"); accent: true; enabled: !auth.busy; onClicked: gate.startDiscord() }
                ProgressBar { Layout.fillWidth: true; visible: auth.busy; indeterminate: auth.progressIndeterminate; value: auth.progress }
                Label { Layout.fillWidth: true; visible: auth.note.length > 0; text: auth.note; textFormat: Text.PlainText; wrapMode: Text.Wrap; color: auth.noteKind === "bad" ? Theme.danger : Theme.textDim; font.pixelSize: 12 }
                CheckBox { text: qsTr("Use manual callback"); checked: gate.manualMode; onToggled: gate.manualMode = checked }
                ColumnLayout {
                    visible: gate.manualMode; Layout.fillWidth: true; spacing: 8
                    Label { text: qsTr("Paste the Discord callback URL or code"); color: Theme.textDim; font.pixelSize: 12 }
                    TextField { id: callbackInput; Layout.fillWidth: true; placeholderText: qsTr("Authorization callback"); selectByMouse: true; onAccepted: gate.finishManual() }
                    ActionButton { text: qsTr("Finish sign-in"); enabled: callbackInput.text.trim().length > 0; onClicked: gate.finishManual() }
                }
                Label { Layout.fillWidth: true; visible: auth.inviteUrl.length > 0; text: qsTr("Need access? Join the configured Discord server."); color: Theme.accentText; font.pixelSize: 12; wrapMode: Text.Wrap
                    TapHandler { onTapped: Qt.openUrlExternally(auth.inviteUrl) }
                }
                Label { Layout.fillWidth: true; visible: auth.setupHint.length > 0 && auth.setupHint !== auth.note; text: auth.setupHint; color: Theme.warn; font.pixelSize: 11; wrapMode: Text.Wrap }
                Label { text: qsTr("Protected by native authorization • No browser renderer"); color: Theme.textDim; font.pixelSize: 10 }
            }
        }
    }
    }
}
