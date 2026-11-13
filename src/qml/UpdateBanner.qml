import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

// The self-update strip. It only exists while there is something to say:
// either a newer build was announced by the Discord releases bot, or the
// download/install of that build is in progress. A failed background check is
// reported on the About page instead, so an offline laptop never gets a red bar
// on every start.
Rectangle {
    id: banner

    readonly property bool offered: updates.available && !updates.skipped

    property bool notesOpen: false

    visible: offered || updates.state === "downloading" || updates.state === "ready"
             || updates.state === "applying"
    implicitHeight: !visible ? 0 : (notesOpen ? 236 : 62)

    radius: 10
    color: Theme.card
    border.width: 1
    border.color: updates.noteKind === "bad" ? Theme.danger : Theme.accent
    clip: true

    Behavior on implicitHeight { NumberAnimation { duration: 140 } }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 12
        anchors.topMargin: 10
        anchors.bottomMargin: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            // pulsing accent dot: "something is new"
            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                width: 10
                height: 10
                radius: 5
                color: Theme.accent

                SequentialAnimation on opacity {
                    running: banner.visible
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.35; duration: 900; easing.type: Easing.InOutQuad }
                    NumberAnimation { to: 1.0; duration: 900; easing.type: Easing.InOutQuad }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    Layout.fillWidth: true
                    text: {
                        if (updates.state === "downloading")
                            return qsTr("Downloading BSI Customizer %1…").arg(updates.latestVersion);
                        if (updates.state === "ready")
                            return qsTr("BSI Customizer %1 is verified and ready").arg(updates.latestVersion);
                        if (updates.state === "applying")
                            return qsTr("Restarting into BSI Customizer %1…").arg(updates.latestVersion);
                        return qsTr("Update %1 is available").arg(updates.latestVersion);
                    }
                    color: Theme.text
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: {
                        if (updates.state === "downloading")
                            return updates.downloadHost.length > 0
                                ? qsTr("%1% of %2 from %3").arg(Math.round(updates.progress * 100))
                                      .arg(updates.downloadSize).arg(updates.downloadHost)
                                : qsTr("%1% of %2").arg(Math.round(updates.progress * 100))
                                      .arg(updates.downloadSize);
                        if (updates.state === "ready")
                            return qsTr("%1 · it replaces the running EXE and restarts the app").arg(updates.downloadSize);
                        if (updates.state === "applying")
                            return qsTr("The window closes first; the new build starts on its own");
                        return qsTr("Through the Discord releases bot · %1").arg(updates.downloadSize);
                    }
                    color: Theme.textDim
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }

            ActionButton {
                visible: updates.releaseNotes.length > 0
                text: banner.notesOpen ? qsTr("Hide notes") : qsTr("What's new")
                onClicked: banner.notesOpen = !banner.notesOpen
            }

            ActionButton {
                visible: updates.state === "available"
                text: updates.readyToInstall ? qsTr("Install and restart") : qsTr("Update now")
                accent: true
                enabled: !backend.busy
                onClicked: updates.readyToInstall ? updates.apply() : updates.installNow()
            }

            ActionButton {
                visible: updates.state === "downloading"
                text: qsTr("Cancel")
                onClicked: updates.cancel()
            }

            ActionButton {
                visible: updates.state === "available"
                text: qsTr("Later")
                onClicked: updates.skip()
            }
        }

        // progress rail
        Rectangle {
            Layout.fillWidth: true
            visible: updates.state === "downloading"
            Layout.preferredHeight: 4
            radius: 2
            color: Theme.raised

            Rectangle {
                width: parent.width * updates.progress
                height: parent.height
                radius: 2
                color: Theme.accent
                Behavior on width { NumberAnimation { duration: 120 } }
            }
        }

        // release notes, expanded in place (no popup to lose track of)
        Flickable {
            Layout.fillWidth: true
            Layout.preferredHeight: banner.notesOpen ? 150 : 0
            visible: banner.notesOpen
            clip: true
            contentHeight: notesText.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            Text {
                id: notesText
                width: parent.width
                text: updates.releaseNotes
                color: Theme.soft
                font.pixelSize: 12
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
            }
        }
    }
}
