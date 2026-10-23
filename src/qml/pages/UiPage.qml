import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import BSICustomizer

Item {
    id: page

    signal notify(string message)

    // how many removal rows are ticked (drives the Apply button state/label)
    property int checkedCount: 0
    // player layout tweaks with their live applied/supported state
    property var layoutTweaks: []
    // the one-click clean-up lives in its own card, so it is pulled out of the
    // generic tweak list below
    property var cleanTweak: ({ id: "clean-player-ui", supported: false, applied: false,
                                needsUpdate: false, label: "", detail: "", state: "" })
    // Settings -> About editors
    property var about: ({ supported: false, applied: false, detail: "", text: "",
                           links: [], showSections: true, sectionsHidden: false })
    // the link rows read from the page, one editable row each
    property var aboutLinks: []
    // whether the two sections the MSI build draws hidden are switched on
    property bool showSectionsWanted: true

    function recountChecked() {
        let n = 0;
        for (let i = 0; i < removalList.count; i++) {
            const it = removalList.itemAtIndex(i);
            if (it !== null && it.checked)
                n++;
        }
        page.checkedCount = n;
    }

    function refreshLayout() {
        const list = backend.layoutTweaks();
        const rest = [];
        let clean = null;
        for (let i = 0; i < list.length; i++) {
            if (list[i].id === "clean-player-ui")
                clean = list[i];
            else
                rest.push(list[i]);
        }
        page.layoutTweaks = rest;
        page.cleanTweak = clean || ({ id: "clean-player-ui", supported: false, applied: false,
                                      needsUpdate: false,
                                      label: qsTr("Clean player UI (one click)"), detail: "",
                                      state: qsTr("Select the HD-Player.exe path first.") });
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

    // Every row as it is edited right now, handed to backend.applyAboutPage().
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
        refreshLayout();
        refreshAbout(true);
    }

    Connections {
        target: backend
        function onExePathChanged() {
            page.refreshLayout();
            page.refreshAbout(true);
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        // ---------------- header ----------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    text: qsTr("UI")
                    color: Theme.text
                    font.pixelSize: 24
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Everything that changes what the player window shows: the one-click "
                               + "clean-up, the sidebar layout, the Settings → About page and the "
                               + "ad/UI strings. A backup is created automatically before any patch.")
                    color: Theme.textDim
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }

            ActionButton {
                Layout.alignment: Qt.AlignTop
                text: qsTr("Open Folder")
                onClicked: settings.openExeFolder()
            }
            ActionButton {
                Layout.alignment: Qt.AlignTop
                text: qsTr("Restore Backup")
                onClicked: backend.restoreBackup(backend.exePath)
            }
        }

        PathRow {
            Layout.fillWidth: true
            label: qsTr("HD-Player.exe")
            text: backend.exePath
            onLoad: backend.exePath = text
        }

        // ---------------- one-click clean-up ----------------
        Card {
            Layout.fillWidth: true
            pad: 14

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text {
                            text: qsTr("Clean player UI (one click)")
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.Bold
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("One apply hides every extra button. Title bar: help, "
                                       + "menu/settings, quests, rewards, account, nowbuX, "
                                       + "pin-to-top, top-bar icons, macro, sync and screen "
                                       + "recording. Sidebar: screenshot, screen record, media "
                                       + "folder, rotate, location, shake, lock cursor, macro, "
                                       + "sync, eco mode, instance manager, side panel, live "
                                       + "stream, chat, settings and its own Back / Home / "
                                       + "Recent. Back, Home, Recent, the BlueStacks logo, the "
                                       + "title/version and the window controls stay, and the "
                                       + "sidebar keeps fullscreen, volume, game controls, "
                                       + "install APK and trim memory — it is not removed. To "
                                       + "remove the sidebar itself, turn on “Sidebar buttons "
                                       + "to the top bar” below.")
                            color: Theme.textDim
                            font.pixelSize: 11
                        }
                    }

                    ActionButton {
                        Layout.alignment: Qt.AlignTop
                        accent: !page.cleanTweak.applied || !!page.cleanTweak.needsUpdate
                        implicitHeight: 30
                        enabled: !!page.cleanTweak.supported
                        text: !page.cleanTweak.supported
                              ? qsTr("Unavailable")
                              : (page.cleanTweak.applied
                                 ? (page.cleanTweak.needsUpdate ? qsTr("Update clean-up")
                                                                : qsTr("Restore stock UI"))
                                 : qsTr("Clean in one click"))
                        onClicked: {
                            // Update keeps the clean-up on and writes the missing half.
                            const target = !(page.cleanTweak.applied
                                             && !page.cleanTweak.needsUpdate);
                            const result = backend.applyLayoutTweak("clean-player-ui", target);
                            page.refreshLayout();
                            if (result && result.msg)
                                page.notify(result.msg);
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: page.cleanTweak.needsUpdate
                           ? Theme.warn
                           : (page.cleanTweak.applied
                              ? Theme.green
                              : (page.cleanTweak.supported ? Theme.textDim : Theme.warn))
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    text: page.cleanTweak.needsUpdate
                          ? qsTr("An older clean-up is applied — press “Update clean-up” to hide "
                                 + "the extra sidebar buttons too.")
                          : (page.cleanTweak.applied
                             ? qsTr("Applied — the extra buttons are hidden and the sidebar keeps "
                                    + "fullscreen, volume, game controls, install APK and trim "
                                    + "memory. Press “Restore stock UI” to bring everything back.")
                             : (page.cleanTweak.supported
                                ? qsTr("Ready — one click hides every button listed above.")
                                : (page.cleanTweak.state || page.cleanTweak.detail)))
                }
            }
        }

        // ---------------- player layout (QML) ----------------
        Card {
            Layout.fillWidth: true
            pad: 14

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Text {
                    text: qsTr("Player layout (QML)")
                    color: Theme.text
                    font.pixelSize: 15
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: qsTr("Structural QML patches that rearrange the player UI. Works with both "
                               + "the current player and older 5.x builds. Reversible - turn an "
                               + "option off and apply again to restore the stock layout.")
                    color: Theme.textDim
                    font.pixelSize: 11
                }

                Repeater {
                    model: page.layoutTweaks
                    delegate: RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        required property var modelData

                        // A tick means "this is the state I want". It starts
                        // from what the EXE currently has, so an applied tweak
                        // unticks to "restore" and a stock one ticks to apply.
                        CheckBox {
                            id: tweakCheck
                            Layout.alignment: Qt.AlignTop
                            checked: !!modelData.applied
                            enabled: modelData.supported
                            padding: 0
                            implicitWidth: 18
                            implicitHeight: 18
                            indicator: Rectangle {
                                implicitWidth: 18
                                implicitHeight: 18
                                radius: 5
                                color: tweakCheck.checked ? Theme.accent : "transparent"
                                border.width: 1
                                border.color: tweakCheck.checked
                                              ? Theme.accent
                                              : (tweakCheck.enabled ? Theme.borderStrong
                                                                    : Theme.border)
                                Text {
                                    anchors.centerIn: parent
                                    visible: tweakCheck.checked
                                    text: "\u2713"
                                    color: Theme.primaryText
                                    font.pixelSize: 11
                                    font.weight: Font.Black
                                }
                            }
                        }
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: tweakText.implicitHeight
                            ColumnLayout {
                                id: tweakText
                                anchors.fill: parent
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: modelData.label
                                    color: modelData.supported ? Theme.text : Theme.textDim
                                    font.pixelSize: 12
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: modelData.detail
                                    color: Theme.textDim
                                    font.pixelSize: 11
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: !modelData.supported || modelData.needsUpdate
                                          ? modelData.state
                                          : (modelData.applied
                                             ? modelData.state + qsTr(" Untick to restore this tweak.")
                                             : qsTr("Ready — tick and press Apply."))
                                    color: !modelData.supported ? Theme.warn
                                           : (modelData.applied ? Theme.green : Theme.textDim)
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                }
                            }
                            // The whole description toggles the tick too, so the
                            // option is not a tiny 18px target.
                            MouseArea {
                                anchors.fill: parent
                                enabled: modelData.supported
                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: tweakCheck.checked = !tweakCheck.checked
                            }
                        }
                        ActionButton {
                            Layout.alignment: Qt.AlignTop
                            accent: true
                            implicitHeight: 28
                            enabled: modelData.supported
                                     && (tweakCheck.checked !== !!modelData.applied
                                         || (modelData.needsUpdate && tweakCheck.checked))
                            text: !modelData.supported
                                  ? qsTr("Unavailable")
                                  : (modelData.applied
                                     ? (tweakCheck.checked
                                        ? (modelData.needsUpdate ? qsTr("Update") : qsTr("Applied"))
                                        : qsTr("Restore"))
                                     : qsTr("Apply"))
                            onClicked: {
                                const result = backend.applyLayoutTweak(modelData.id,
                                                                        tweakCheck.checked);
                                page.refreshLayout();
                                if (result && result.msg)
                                    page.notify(result.msg);
                            }
                        }
                    }
                }
            }
        }

        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            pad: 14

            ColumnLayout {
                anchors.fill: parent
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: qsTr("Ads / UI Removal")
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.Bold
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("Nulls out the selected ad/UI strings in the binary so "
                                       + "those elements never render.")
                            color: Theme.textDim
                            font.pixelSize: 11
                        }
                    }
                    ActionButton {
                        Layout.alignment: Qt.AlignTop
                        text: qsTr("Select All")
                        implicitHeight: 28
                        onClicked: {
                            const on = page.checkedCount < removalList.count;
                            for (let i = 0; i < removalList.count; i++) {
                                const it = removalList.itemAtIndex(i);
                                if (it !== null)
                                    it.checked = on;
                            }
                            page.recountChecked();
                        }
                    }
                    ActionButton {
                        Layout.alignment: Qt.AlignTop
                        accent: true
                        enabled: page.checkedCount > 0
                        implicitHeight: 28
                        text: page.checkedCount > 0
                              ? qsTr("Apply Removals (%1)").arg(page.checkedCount)
                              : qsTr("Apply Removals")
                        onClicked: {
                            const names = [];
                            for (let i = 0; i < removalList.count; i++) {
                                const it = removalList.itemAtIndex(i);
                                if (it !== null && it.checked)
                                    names.push(removalList.model.get(i).name);
                            }
                            backend.applyRemovals(names);
                        }
                    }
                }

                ListView {
                    id: removalList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: ListModel {}
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    Component.onCompleted: {
                        for (const n of backend.removalNames)
                            model.append({ name: n });
                    }

                    delegate: Rectangle {
                        id: rowRect
                        required property var model
                        required property int index
                        property bool checked: false
                        width: removalList.width
                        height: 34
                        radius: 8
                        color: checked ? Theme.accentSoft : Theme.raised
                        border.width: 1
                        border.color: checked ? Theme.accent : Theme.border

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 8
                            Rectangle {
                                Layout.preferredWidth: 16
                                Layout.preferredHeight: 16
                                radius: 4
                                color: rowRect.checked ? Theme.accent : "transparent"
                                border.width: 1
                                border.color: rowRect.checked ? Theme.accent : Theme.border
                                Text {
                                    anchors.centerIn: parent
                                    visible: rowRect.checked
                                    text: "\u2713"
                                    color: "#ffffff"
                                    font.pixelSize: 10
                                    font.weight: Font.Black
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: rowRect.model.name
                                color: Theme.text
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                rowRect.checked = !rowRect.checked;
                                page.recountChecked();
                            }
                        }
                    }
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
