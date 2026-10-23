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
    property var about: ({ supported: false, applied: false, detail: "", text: "",
                           links: [], showSections: true, sectionsHidden: false })
    // Whether the patch switches the two sections back on.
    property bool showSectionsWanted: true
    // One entry per stock link row: { id, name, url, hidden }.
    property var aboutLinks: []

    function refreshClean() {
        const list = backend.layoutTweaks();
        let found = null;
        for (let i = 0; i < list.length; i++) {
            if (list[i].id === "clean-player-ui")
                found = list[i];
        }
        page.cleanTweak = found || ({ id: "clean-player-ui", supported: false, applied: false,
                                      label: qsTr("Extra top-bar buttons"),
                                      detail: qsTr("Select the HD-Player.exe path first."),
                                      state: qsTr("Unavailable") });
    }

    function refreshAbout(loadFields) {
        const state = backend.aboutPageState();
        page.about = state;
        if (loadFields) {
            page.aboutLinks = state.links || [];
            aboutText.text = state.text || "";
            page.showSectionsWanted = state.showSections !== false;
        }
    }

    // The editors are the source of truth when applying: read what is on
    // screen instead of tracking every keystroke in a model.
    function collectLinks() {
        const out = [];
        for (let i = 0; i < linkRepeater.count; i++) {
            const item = linkRepeater.itemAt(i);
            if (!item)
                continue;
            out.push({ id: item.linkId, name: item.linkName, url: item.linkUrl,
                       hidden: item.linkHidden });
        }
        return out;
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
                                text: qsTr("Extra top-bar buttons (one click)")
                                color: Theme.text
                                font.pixelSize: 15
                                font.weight: Font.Bold
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: qsTr("One apply hides the extra title-bar buttons — help, "
                                           + "menu/settings, rewards, account, top-bar icons, "
                                           + "macro, sync and screen recording. Back, Home, Recent, "
                                           + "the BlueStacks logo, the title/version and the window "
                                           + "controls stay. The sidebar itself is not touched: to "
                                           + "remove it, turn on “Sidebar buttons to the top bar” "
                                           + "on the Extras page — that moves the sidebar into the "
                                           + "title bar.")
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
                              ? qsTr("Applied — the extra top-bar buttons are hidden and the "
                                     + "sidebar is untouched. Press “Restore stock UI” to bring "
                                     + "the stock title bar back.")
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
                                text: qsTr("Everything below is read from the About page itself: "
                                           + "the sentence it prints and every link with its name "
                                           + "and address. Edit them, hide the ones you do not "
                                           + "want and apply — the page keeps its layout, logo and "
                                           + "version and the “Check for updates” button stays "
                                           + "disabled.")
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
                                const result = backend.applyAboutPage("", [], false, false);
                                page.refreshAbout(true);
                                if (result && result.msg) {
                                    page.notify(result.msg);
                                    console.log(result.msg);
                                }
                            }
                        }
                    }

                    // ---- the sections the MSI build hides -----------------
                    // On MSI 5 both sections are bound to `oem !== "msi5"`, so they are
                    // drawn hidden and the links cannot be seen or used. This button is
                    // the first step there: it switches them back on, then the stock
                    // links and the About text below can be edited and applied.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ActionButton {
                            accent: !page.showSectionsWanted && !!page.about.supported
                            implicitHeight: 30
                            enabled: !!page.about.supported
                            text: page.showSectionsWanted
                                  ? qsTr("Leave the sections as the build draws them")
                                  : qsTr("Unhide links + About")
                            onClicked: page.showSectionsWanted = !page.showSectionsWanted
                        }

                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            font.pixelSize: 11
                            color: page.showSectionsWanted ? Theme.textDim : Theme.warn
                            text: {
                                if (!page.about.supported)
                                    return "";
                                if (!page.about.sectionsHidden)
                                    return qsTr("This build already shows both sections; the page "
                                                + "text and the rows below are editable as they are.");
                                return page.showSectionsWanted
                                    ? qsTr("This build hides “Important links” and “About and other "
                                           + "info” (oem !== \"msi5\"). Applying switches both back "
                                           + "on, so the text and the rows below are the ones shown.")
                                    : qsTr("Both sections stay hidden, exactly as the build draws "
                                           + "them. Press “Unhide links + About” to bring them back.");
                            }
                        }
                    }

                    // ---- the About sentence -------------------------------
                    Text {
                        text: qsTr("About text — exactly what the page prints now")
                        color: Theme.textDim
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    TextArea {
                        id: aboutText
                        Layout.fillWidth: true
                        Layout.preferredHeight: 96
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

                    // ---- the stock links ----------------------------------
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            text: qsTr("Links — name and address, untick “Show” to remove one")
                            color: Theme.textDim
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            visible: page.aboutLinks.length === 0
                            text: page.about.supported ? qsTr("No stock link rows in this player")
                                                       : qsTr("Select HD-Player.exe first")
                            color: Theme.warn
                            font.pixelSize: 10
                        }
                    }

                    Repeater {
                        id: linkRepeater
                        model: page.aboutLinks

                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8

                            // Read back by collectLinks() when Apply is pressed.
                            property string linkId: modelData.id
                            property alias linkName: linkNameField.text
                            property alias linkUrl: linkUrlField.text
                            property alias linkHidden: linkShow.checked

                            CheckBox {
                                id: linkShow
                                text: qsTr("Show")
                                checked: !modelData.hidden
                                font.pixelSize: 11
                                palette.windowText: Theme.text
                                palette.buttonText: Theme.text
                                property bool checkedToggle: checked
                            }
                            TextField {
                                id: linkNameField
                                Layout.preferredWidth: 190
                                text: modelData.name
                                placeholderText: qsTr("Support email")
                                color: Theme.text
                                placeholderTextColor: Theme.textDim
                                font.pixelSize: 12
                                background: Rectangle {
                                    color: Theme.raised
                                    radius: 8
                                    border.width: 1
                                    border.color: linkNameField.activeFocus ? Theme.accent : Theme.border
                                }
                            }
                            TextField {
                                id: linkUrlField
                                Layout.fillWidth: true
                                text: modelData.url
                                placeholderText: qsTr("https://… or name@site.com")
                                color: Theme.text
                                placeholderTextColor: Theme.textDim
                                font.pixelSize: 12
                                font.family: "Consolas"
                                background: Rectangle {
                                    color: Theme.raised
                                    radius: 8
                                    border.width: 1
                                    border.color: linkUrlField.activeFocus ? Theme.accent : Theme.border
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
                            text: page.about.applied ? qsTr("Update About page")
                                                     : qsTr("Apply About page")
                            onClicked: {
                                const result = backend.applyAboutPage(aboutText.text,
                                                                      page.collectLinks(),
                                                                      page.showSectionsWanted,
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
                                  ? qsTr("Applied. Edit any field and press Update to change it.")
                                  : (page.about.supported
                                     ? qsTr("Ready — the text and links above are what the page "
                                            + "prints now; the update button will be disabled.")
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
                        visible: !!page.about.supported && page.about.sectionsHidden
                                 && !page.showSectionsWanted
                        text: qsTr("Heads-up: this build hides both sections, so the text and links "
                                   + "you apply now will not be visible until you press “Unhide "
                                   + "links + About”.")
                        color: Theme.warn
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Only the About page is edited; every other Settings tab stays "
                                   + "stock. A row whose “Show” is unticked is hidden on the page, "
                                   + "and an address written as an e-mail opens the mail app. "
                                   + "Restart BlueStacks after applying.")
                        color: Theme.textDim
                        font.pixelSize: 10
                        font.italic: true
                    }
                }
            }
        }
    }
}
