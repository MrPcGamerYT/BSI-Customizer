import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Item {
    id: page

    property string updateFailure: ""
    signal notify(string message)

    Component.onCompleted: {
        const reason = updates.helperFailure();
        if (reason && reason.length > 0)
            page.updateFailure = reason;
    }

    function trustText(trust) {
        if (trust === "signed")
            return qsTr("Signed release: the download is checked against the publisher's signed SHA-256 manifest.");
        if (trust === "pinned")
            return qsTr("Hash-pinned release: the download is checked against the SHA-256 published with the release.");
        if (trust === "first-use")
            return qsTr("First-use fingerprint: this PC records the SHA-256 so the same version cannot silently change later.");
        return "";
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            x: 22
            y: 18
            width: parent.width - 44
            spacing: 13

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    text: qsTr("Updates")
                    color: Theme.text
                    font.pixelSize: 18
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Check, review and install BSI Customizer releases. Nothing downloads until you choose it.")
                    color: Theme.textDim
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
            }

            Card {
                Layout.fillWidth: true
                pad: 15
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                text: qsTr("Release status")
                                color: Theme.text
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            Text {
                                text: qsTr("Installed %1").arg(updates.currentVersion)
                                color: Theme.textDim
                                font.pixelSize: 10
                                font.family: "Consolas"
                            }
                        }
                        Rectangle {
                            visible: updates.available
                            Layout.preferredHeight: 28
                            Layout.preferredWidth: latestLabel.implicitWidth + 18
                            radius: 9
                            color: Theme.accentSoft
                            border.width: 1
                            border.color: Theme.border
                            Text {
                                id: latestLabel
                                anchors.centerIn: parent
                                text: qsTr("Latest %1").arg(updates.latestVersion)
                                color: Theme.accentText
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: updates.available && updates.trust.length > 0
                        text: page.trustText(updates.trust)
                        color: updates.trust === "first-use" ? Theme.warn : Theme.textDim
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: updates.note.length > 0
                        text: updates.note
                        color: updates.noteKind === "bad" ? Theme.danger
                             : updates.noteKind === "warn" ? Theme.warn
                             : updates.noteKind === "good" ? Theme.green : Theme.textDim
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: !updates.configured && updates.note !== updates.setupHint
                        text: updates.setupHint
                        color: Theme.textDim
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        visible: page.updateFailure.length > 0
                        implicitHeight: failureColumn.implicitHeight + 16
                        radius: 9
                        color: Qt.alpha(Theme.warn, 0.12)
                        border.width: 1
                        border.color: Theme.warn
                        ColumnLayout {
                            id: failureColumn
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 6
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("The last update could not be installed")
                                color: Theme.warn
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }
                            Text {
                                Layout.fillWidth: true
                                text: page.updateFailure
                                color: Theme.text
                                font.pixelSize: 10
                                font.family: "Consolas"
                                wrapMode: Text.WrapAnywhere
                            }
                            RowLayout {
                                spacing: 7
                                ActionButton { text: qsTr("Update log"); onClicked: updates.showHelperLog() }
                                ActionButton { text: qsTr("Release page"); onClicked: updates.openDownloadPage() }
                                ActionButton { text: qsTr("Dismiss"); onClicked: page.updateFailure = "" }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 7
                        ActionButton {
                            text: updates.checking ? qsTr("Checking…") : qsTr("Check now")
                            enabled: updates.configured && !updates.busy
                            onClicked: updates.check()
                        }
                        ActionButton {
                            visible: updates.available && !updates.readyToInstall
                            text: qsTr("Download %1").arg(updates.latestVersion)
                            accent: true
                            onClicked: updates.download()
                        }
                        ActionButton {
                            visible: updates.available && updates.skipped
                            text: qsTr("Offer again")
                            onClicked: updates.unskip()
                        }
                        ActionButton {
                            visible: updates.readyToInstall
                            text: qsTr("Install & restart")
                            accent: true
                            enabled: !backend.busy
                            onClicked: updates.apply()
                        }
                        ActionButton {
                            visible: updates.readyToInstall
                            text: qsTr("Show file")
                            onClicked: updates.showStagedFile()
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            visible: updates.busy
                            text: Math.round(updates.progress * 100) + "%"
                            color: Theme.textDim
                            font.pixelSize: 10
                            font.family: "Consolas"
                        }
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                pad: 14
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    Text {
                        text: qsTr("Automatic checks")
                        color: Theme.text
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    ToggleSwitch {
                        Layout.fillWidth: true
                        label: qsTr("Check for updates when the app starts")
                        description: qsTr("Checks release information after launch. A download only starts when you choose it.")
                        checked: updates.autoCheck
                        onToggled: updates.autoCheck = !updates.autoCheck
                    }
                }
            }

            Item { Layout.preferredHeight: 4 }
        }
    }
}
