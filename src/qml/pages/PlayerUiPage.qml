import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Item {
    id: page

    signal notify(string message)

    // Live state of the one-click player clean-up (LayoutPatcher) and of the
    // Settings → About replacement (AboutPatcher).
    property var cleanTweak: ({ id: "clean-player-ui", supported: false, applied: false,
                                label: "", detail: "", state: "" })
    property var about: ({ supported: false, applied: false, detail: "",
                           text: "", linkLabel: "", linkUrl: "" })

    function refreshClean() {
        const list = backend.layoutTweaks();
        let found = null;
        for (let i = 0; i < list.length; i++) {
            if (list[i].id === "clean-player-ui")
                found = list[i];
        }
        page.cleanTweak = found || ({ id: "clean-player-ui", supported: false, applied: false,
                                      label: qsTr("Clean player UI"),
                                      detail: qsTr("Select the HD-Player.exe path first."),
                                      state: qsTr("Unavailable") });
    }

    function refreshAbout(loadFields) {
        const state = backend.aboutPageState();
        page.about = state;
        if (loadFields) {
            aboutText.text = state.text || "";
            aboutLabel.text = state.linkLabel || "";
            aboutUrl.text = state.linkUrl || "";
        }
    }

    Component.onCompleted: {
        refreshClean();
        refreshAbout(true);
    }

    Connections {
        target: backend
        function onExePathChanged() {
            page.refreshClean();
            page.refreshAbout(true);
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: content.implicitHeight + 48
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: content
            x: 24
            y: 24
            width: parent.width - 48
            spacing: 16

            Text {
                text: qsTr("Player UI")
                color: Theme.text
                font.pixelSize: 24
                font.weight: Font.Bold
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("One-click window clean-up and a custom Settings → About page for "
                           + "HD-Player.exe. A backup is created automatically before any patch; "
                           + "close BlueStacks first.")
                color: Theme.textDim
                font.pixelSize: 12
            }

            // ---------------- one-click clean-up ----------------
            Card {
                Layout.fillWidth: true
                pad: 18

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                text: qsTr("Sidebar + extra button remover")
                                color: Theme.text
                                font.pixelSize: 15
                                font.weight: Font.Bold
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: qsTr("One apply removes the whole sidebar and every extra top-bar "
                                           + "button — help, menu/settings, rewards, account, top-bar "
                                           + "icons and the sidebar toggle. Back, Home, Recent, the "
                                           + "BlueStacks logo, the title/version and the window "
                                           + "controls stay.")
                                color: Theme.textDim
                                font.pixelSize: 11
                            }
                        }

                        ActionButton {
                            Layout.alignment: Qt.AlignTop
                            accent: !page.cleanTweak.applied
                            implicitHeight: 30
                            enabled: !!page.cleanTweak.supported
                            text: !page.cleanTweak.supported
                                  ? qsTr("Unavailable")
                                  : (page.cleanTweak.applied ? qsTr("Restore stock UI")
                                                             : qsTr("Clean in one click"))
                            onClicked: {
                                const result = backend.applyLayoutTweak("clean-player-ui",
                                                                        !page.cleanTweak.applied);
                                page.refreshClean();
                                if (result && result.msg) {
                                    page.notify(result.msg);
                                    console.log(result.msg);
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: page.cleanTweak.applied
                              ? qsTr("Applied — the sidebar and the extra top-bar buttons are "
                                     + "hidden. Press “Restore stock UI” to bring them back.")
                              : (page.cleanTweak.supported
                                 ? qsTr("Ready — one click hides everything listed above.")
                                 : (page.cleanTweak.state || page.cleanTweak.detail))
                        color: page.cleanTweak.applied ? Theme.green
                               : (page.cleanTweak.supported ? Theme.textDim : Theme.warn)
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }
            }

            // ---------------- Settings → About ----------------
            Card {
                Layout.fillWidth: true
                pad: 18

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                text: qsTr("Settings → About page")
                                color: Theme.text
                                font.pixelSize: 15
                                font.weight: Font.Bold
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: qsTr("Keeps the About page layout and logo/version, replaces "
                                           + "BlueStacks' stock About text with yours, replaces the "
                                           + "stock support / website / QQ links with one link of "
                                           + "yours, and disables the “Check for updates” button.")
                                color: Theme.textDim
                                font.pixelSize: 11
                            }
                        }

                        ActionButton {
                            Layout.alignment: Qt.AlignTop
                            text: qsTr("Restore stock About")
                            implicitHeight: 30
                            enabled: !!page.about.applied
                            onClicked: {
                                const result = backend.applyAboutPage("", "", "", false);
                                page.refreshAbout(true);
                                if (result && result.msg) {
                                    page.notify(result.msg);
                                    console.log(result.msg);
                                }
                            }
                        }
                    }

                    Text {
                        text: qsTr("About text (replaces the stock copyright / Terms line)")
                        color: Theme.textDim
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    TextArea {
                        id: aboutText
                        Layout.fillWidth: true
                        Layout.preferredHeight: 106
                        wrapMode: TextArea.Wrap
                        selectByMouse: true
                        placeholderText: qsTr("e.g. Modded by Mr.Pc Gamer — join the channel for "
                                              + "new builds, themes and tools.")
                        color: Theme.text
                        placeholderTextColor: Theme.textDim
                        font.pixelSize: 12
                        background: Rectangle {
                            color: Theme.raised
                            radius: 8
                            border.width: 1
                            border.color: aboutText.activeFocus ? Theme.accent : Theme.border
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text {
                                text: qsTr("Your link label (optional)")
                                color: Theme.textDim
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }
                            TextField {
                                id: aboutLabel
                                Layout.fillWidth: true
                                placeholderText: qsTr("Join my channel")
                                color: Theme.text
                                placeholderTextColor: Theme.textDim
                                font.pixelSize: 12
                                background: Rectangle {
                                    color: Theme.raised
                                    radius: 8
                                    border.width: 1
                                    border.color: aboutLabel.activeFocus ? Theme.accent : Theme.border
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text {
                                text: qsTr("Link URL (optional)")
                                color: Theme.textDim
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }
                            TextField {
                                id: aboutUrl
                                Layout.fillWidth: true
                                placeholderText: qsTr("https://…")
                                color: Theme.text
                                placeholderTextColor: Theme.textDim
                                font.pixelSize: 12
                                font.family: "Consolas"
                                background: Rectangle {
                                    color: Theme.raised
                                    radius: 8
                                    border.width: 1
                                    border.color: aboutUrl.activeFocus ? Theme.accent : Theme.border
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ActionButton {
                            accent: true
                            implicitHeight: 30
                            enabled: !!page.about.supported
                                     && (aboutText.text.trim() !== "" || aboutUrl.text.trim() !== "")
                            text: page.about.applied ? qsTr("Update About page")
                                                     : qsTr("Apply About page")
                            onClicked: {
                                const result = backend.applyAboutPage(aboutText.text,
                                                                      aboutLabel.text,
                                                                      aboutUrl.text,
                                                                      true);
                                page.refreshAbout(true);
                                if (result && result.msg) {
                                    page.notify(result.msg);
                                    console.log(result.msg);
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: page.about.applied
                                  ? qsTr("Applied. Edit the fields and press Update to change it.")
                                  : (page.about.supported
                                     ? qsTr("Ready — the stock texts and links are replaced and the "
                                            + "update button is disabled.")
                                     : (page.about.detail || qsTr("Unavailable")))
                            color: page.about.applied ? Theme.green
                                   : (page.about.supported ? Theme.textDim : Theme.warn)
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Only the About page is edited; every other Settings tab stays "
                                   + "stock. Leave the link fields empty to keep your text and hide "
                                   + "the stock links. Restart BlueStacks after applying.")
                        color: Theme.textDim
                        font.pixelSize: 10
                        font.italic: true
                    }
                }
            }
        }
    }
}
