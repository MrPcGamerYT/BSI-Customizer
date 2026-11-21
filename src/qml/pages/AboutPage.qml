import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer
import "../Icons.js" as Icons

Item {
    id: page

    signal notify(string message)

    property bool editingProfile: false
    // Working copy of the About buttons while the editor is open. Each entry is
    // { kind, title, url }; kinds are "github", "discord", "youtube" or
    // "custom" (generic link icon). Up to eight buttons, any label and URL.
    property var draftLinks: []

    function iconFor(kind, color) {
        if (kind === "discord") return Icons.icon(Icons.discord, color);
        if (kind === "youtube") return Icons.icon(Icons.youtube, color);
        if (kind === "custom") return Icons.icon(Icons.link, color);
        return Icons.icon(Icons.github, color);
    }

    function beginProfileEdit() {
        page.draftLinks = settings.aboutLinks.map(l => ({ kind: l.kind, title: l.title, url: l.url }));
        page.editingProfile = true;
        developerNameInput.text = settings.aboutDeveloperName;
        taglineInput.text = settings.aboutTagline;
    }

    function captureDraftLinks() {
        const rows = [];
        for (let i = 0; i < page.draftLinks.length; ++i) {
            const row = profileLinkRepeater.itemAt(i);
            const kinds = ["github", "discord", "youtube", "custom"];
            rows.push({
                kind: row ? kinds[Math.max(0, row.kindBox.currentIndex)] : page.draftLinks[i].kind,
                title: row ? row.titleInput.text : (page.draftLinks[i].title || ""),
                url: row ? row.urlInput.text : (page.draftLinks[i].url || "")
            });
        }
        page.draftLinks = rows;
    }

    function addProfileLink() {
        page.captureDraftLinks();
        if (page.draftLinks.length >= 8) {
            page.notify(qsTr("You can use up to eight About buttons."));
            return;
        }
        page.draftLinks = page.draftLinks.concat([{ kind: "custom", title: "", url: "" }]);
    }

    function removeProfileLink(index) {
        page.captureDraftLinks();
        const copy = page.draftLinks.slice();
        copy.splice(index, 1);
        page.draftLinks = copy;
    }

    function saveProfile() {
        page.captureDraftLinks();
        const links = page.draftLinks.map(l => ({ kind: l.kind, title: l.title.trim(), url: l.url.trim() }));
        for (let i = 0; i < links.length; ++i) {
            if ((links[i].title.length === 0) !== (links[i].url.length === 0)) {
                page.notify(qsTr("For each link, enter both a label and an HTTP or HTTPS URL, or leave both blank."));
                return;
            }
            if (links[i].url.length > 0 && !/^https?:\/\//i.test(links[i].url)) {
                page.notify(qsTr("Links must start with https:// or http://."));
                return;
            }
        }
        settings.aboutDeveloperName = developerNameInput.text.trim();
        settings.aboutTagline = taglineInput.text.trim();
        settings.aboutLinks = links;
        page.editingProfile = false;
        page.notify(qsTr("About profile saved on this PC."));
    }

    function resetProfile() {
        settings.aboutDeveloperName = qsTr("Mr.Pc Gamer & Romulos");
        settings.aboutTagline = qsTr("BlueStacks tools · with contributor Nova");
        settings.aboutLinks = [
            { kind: "github", title: qsTr("Open-source project"), url: "https://github.com/MrPcGamerYT/BSI-Customizer" },
            { kind: "discord", title: qsTr("Community Discord"), url: "https://discord.gg/5qkKPRZkWa" },
            { kind: "youtube", title: qsTr("Mr.Pc Gamer on YouTube"), url: "https://www.youtube.com/@MR.PC_GAMER_YT?sub_confirmation=1" },
            { kind: "youtube", title: qsTr("Romulos on YouTube"), url: "https://www.youtube.com/@Romulus_FF?sub_confirmation=1" }
        ];
        page.draftLinks = [];
        page.editingProfile = false;
        page.notify(qsTr("Default About profile restored."));
    }

    function openLink(url) {
        const target = String(url || "").trim();
        if (/^https?:\/\//i.test(target))
            Qt.openUrlExternally(target);
    }


    Flickable {
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: content.implicitHeight + 36
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: content
            x: 22
            y: 18
            width: parent.width - 44
            spacing: 13

            // Centered app identity: icon first, then the editable developer
            // identity and the custom link-button grid.
            Card {
                Layout.fillWidth: true
                pad: 18
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 9

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 78
                        Layout.preferredHeight: 78
                        radius: 23
                        color: Theme.cardLow
                        border.width: 1
                        border.color: Theme.borderStrong
                        Image {
                            anchors.fill: parent
                            anchors.margins: 7
                            source: "img/app-icon.png"
                            sourceSize: Qt.size(128, 128)
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                        }
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("BSI CUSTOMIZER")
                        color: Theme.text
                        font.pixelSize: 20
                        font.weight: Font.Black
                        font.letterSpacing: 1.1
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: settings.aboutDeveloperName
                        color: Theme.accentText
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: settings.aboutTagline
                        color: Theme.textDim
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Version %1").arg(settings.version)
                        color: Theme.textDim
                        font.pixelSize: 10
                        font.family: "Consolas"
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 2
                        columns: width < 520 ? 1 : 2
                        rowSpacing: 8
                        columnSpacing: 8

                        Repeater {
                            model: settings.aboutLinks
                            delegate: Rectangle {
                                id: profileLink
                                required property var modelData
                                required property int index
                                visible: !!modelData.title && !!modelData.url
                                Layout.fillWidth: true
                                Layout.preferredHeight: visible ? 54 : 0
                                radius: 11
                                color: linkArea.containsMouse ? Theme.accentSoft : Theme.cardLow
                                border.width: 1
                                border.color: linkArea.containsMouse ? Theme.borderStrong : Theme.border
                                Behavior on color { ColorAnimation { duration: Theme.motionDuration } }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 11
                                    anchors.rightMargin: 11
                                    spacing: 10
                                    Rectangle {
                                        Layout.preferredWidth: 30
                                        Layout.preferredHeight: 30
                                        radius: 9
                                        color: Theme.accentSoft
                                        Image {
                                            anchors.centerIn: parent
                                            width: 16
                                            height: 16
                                            source: page.iconFor(profileLink.modelData.kind, Theme.accentText)
                                            smooth: true
                                        }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text {
                                            Layout.fillWidth: true
                                            text: profileLink.modelData.title
                                            color: linkArea.containsMouse ? Theme.accentText : Theme.text
                                            font.pixelSize: 11
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: profileLink.modelData.url
                                            color: Theme.textDim
                                            font.pixelSize: 9
                                            elide: Text.ElideMiddle
                                        }
                                    }
                                    Text {
                                        text: "↗"
                                        color: Theme.textDim
                                        font.pixelSize: 14
                                    }
                                }
                                MouseArea {
                                    id: linkArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: page.openLink(profileLink.modelData.url)
                                }
                            }
                        }
                    }

                    ActionButton {
                        Layout.alignment: Qt.AlignHCenter
                        text: page.editingProfile ? qsTr("Close editor") : qsTr("Customize About page")
                        onClicked: {
                            if (page.editingProfile)
                                page.editingProfile = false;
                            else
                                page.beginProfileEdit();
                        }
                    }
                }
            }

            // All visible profile text and every custom button link can be
            // adjusted here and are stored in this user's QSettings profile.
            Card {
                Layout.fillWidth: true
                visible: page.editingProfile
                pad: 14
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 9
                    Text {
                        text: qsTr("Customize About")
                        color: Theme.text
                        font.pixelSize: 13
                        font.weight: Font.Bold
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: width < 560 ? 1 : 2
                        columnSpacing: 10
                        rowSpacing: 8
                        StudioTextField {
                            id: developerNameInput
                            Layout.fillWidth: true
                            placeholderText: qsTr("Developer name")
                            color: Theme.text
                            font.pixelSize: 11
                            background: Rectangle { color: Theme.cardLow; radius: 8; border.color: Theme.border }
                        }
                        StudioTextField {
                            id: taglineInput
                            Layout.fillWidth: true
                            placeholderText: qsTr("Short tagline")
                            color: Theme.text
                            font.pixelSize: 11
                            background: Rectangle { color: Theme.cardLow; radius: 8; border.color: Theme.border }
                        }
                    }
                    Repeater {
                        id: profileLinkRepeater
                        model: page.draftLinks
                        delegate: RowLayout {
                            id: linkEditor
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            spacing: 8
                            ComboBox {
                                id: linkKindBox
                                property var kinds: ["github", "discord", "youtube", "custom"]
                                Layout.preferredWidth: 104
                                model: [qsTr("GitHub"), qsTr("Discord"), qsTr("YouTube"), qsTr("Custom")]
                                currentIndex: Math.max(0, kinds.indexOf(linkEditor.modelData.kind || "custom"))
                                font.pixelSize: 10
                                contentItem: Text {
                                    text: linkKindBox.displayText
                                    color: Theme.text
                                    font.pixelSize: 10
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                                background: Rectangle {
                                    color: Theme.cardLow
                                    radius: 7
                                    border.color: Theme.border
                                }
                            }
                            StudioTextField {
                                id: linkTitleInput
                                property alias value: linkTitleInput.text
                                Layout.preferredWidth: 205
                                Layout.fillWidth: true
                                text: linkEditor.modelData.title || ""
                                placeholderText: qsTr("Button label")
                                color: Theme.text
                                font.pixelSize: 10
                                background: Rectangle { color: Theme.cardLow; radius: 7; border.color: Theme.border }
                            }
                            StudioTextField {
                                id: linkUrlInput
                                property alias value: linkUrlInput.text
                                Layout.fillWidth: true
                                text: linkEditor.modelData.url || ""
                                placeholderText: "https://…"
                                color: Theme.text
                                font.pixelSize: 10
                                background: Rectangle { color: Theme.cardLow; radius: 7; border.color: Theme.border }
                            }
                            ActionButton {
                                text: "\u2715"
                                onClicked: page.removeProfileLink(linkEditor.index)
                            }
                            property alias titleInput: linkTitleInput
                            property alias urlInput: linkUrlInput
                            property alias kindBox: linkKindBox
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ActionButton {
                            text: page.draftLinks.length >= 8 ? qsTr("8 buttons max") : qsTr("+ Add button")
                            enabled: page.draftLinks.length < 8
                            onClicked: page.addProfileLink()
                        }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ActionButton {
                            text: qsTr("Save profile")
                            accent: true
                            onClicked: page.saveProfile()
                        }
                        ActionButton {
                            text: qsTr("Use defaults")
                            onClicked: page.resetProfile()
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            Item { Layout.preferredHeight: 4 }
        }
    }
}
