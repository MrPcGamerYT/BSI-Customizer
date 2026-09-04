import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Item {
    id: page

    property bool qmlMethod: methodChips.currentIndex === 0
    property bool fileChanged: backend.themeFileChanged()
    property string filterText: ""
    property string presetId: ""

    // Bound to backend.themeEntries, so the counters update when the theme is
    // re-read or an edit is staged (a plain function call in a binding does not).
    readonly property int editedCount: {
        let n = 0;
        for (const e of backend.themeEntries)
            if (e.edit !== "")
                n++;
        return n;
    }

    readonly property var filteredEntries: {
        const all = backend.themeEntries;
        const f = page.filterText.trim().toLowerCase();
        if (f === "")
            return all;
        return all.filter(function (e) {
            return String(e.name).toLowerCase().indexOf(f) >= 0
                    || String(e.purpose).toLowerCase().indexOf(f) >= 0
                    || String(e.value).toLowerCase().indexOf(f) >= 0
                    || String(e.edit).toLowerCase().indexOf(f) >= 0;
        });
    }

    function reload() {
        backend.loadTheme(page.qmlMethod ? "qml" : "hex");
        page.fileChanged = backend.themeFileChanged();
    }

    // Load on the first visit, and re-read the EXE whenever it changed on disk
    // (another theme tool patched it, BlueStacks updated itself, a restore
    // happened).  Only then - so pending color edits survive a page switch, and
    // never thrown away silently: with edits pending we show the stale banner
    // instead and let the user press Reload.
    // Without this the list keeps showing the colors from the last load, which
    // is exactly "the tool shows a color I applied previously".
    onVisibleChanged: {
        if (!visible)
            return;
        if (!backend.themeFound)
            page.reload();
        else if (page.fileChanged && page.editedCount === 0)
            page.reload();
    }

    // Catch an external change while the page is open on screen.
    Timer {
        interval: 2500
        repeat: true
        running: page.visible
        onTriggered: page.fileChanged = backend.themeFileChanged()
    }

    Connections {
        target: backend
        function onThemeChanged() { page.fileChanged = backend.themeFileChanged() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        // ---------------- header ----------------
        RowLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    text: qsTr("Theme Changer")
                    color: Theme.text
                    font.pixelSize: 24
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    text: backend.themeStatus.length > 0
                          ? backend.themeStatus
                          : qsTr("Select HD-Player.exe to begin.")
                    color: Theme.textDim
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: page.fileChanged && backend.themeFound
                    text: qsTr("HD-Player.exe changed on disk - the colors below are stale. "
                               + "Press Reload to read the theme BlueStacks actually has.")
                    color: Theme.logOrange
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }

            ActionButton {
                Layout.alignment: Qt.AlignTop
                text: qsTr("Reload")
                accent: page.fileChanged
                onClicked: page.reload()
            }
            ChoiceChips {
                id: methodChips
                Layout.alignment: Qt.AlignTop
                model: [qsTr("QML (new)"), qsTr("Hex (old)")]
                onSelected: (index) => {
                    backend.loadTheme(index === 0 ? "qml" : "hex");
                    page.fileChanged = backend.themeFileChanged();
                }
            }
        }

        PathRow {
            Layout.fillWidth: true
            label: qsTr("HD-Player.exe")
            showLoad: true
            text: backend.exePath
            onLoad: {
                backend.exePath = text;
                page.reload();
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: page.qmlMethod ? 0 : 1

            // ---------------- QML (zlib) method ----------------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10

                // ---- presets ----
                Card {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 168

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Text {
                                text: qsTr("Theme presets")
                                color: Theme.text
                                font.pixelSize: 15
                                font.weight: Font.Bold
                            }
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: !backend.themeFound
                                      ? qsTr("Load the theme from HD-Player.exe first - "
                                             + "then pick a palette.")
                                      : (page.presetId === ""
                                         ? qsTr("Pick a palette, tweak any color, then Patch.")
                                         : qsTr("Preset staged - press Patch Theme to write it."))
                                color: Theme.textDim
                                font.pixelSize: 12
                            }
                            ActionButton {
                                text: qsTr("Clear")
                                onClicked: {
                                    page.presetId = "";
                                    backend.clearThemeEdits();
                                }
                            }
                            ActionButton {
                                text: qsTr("Recolor switches")
                                enabled: page.presetId !== ""
                                onClicked: backend.recolorTogglesFromPreset(page.presetId)
                            }
                        }

                        ListView {
                            id: presetList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            orientation: ListView.Horizontal
                            spacing: 8
                            model: backend.themePresets
                            opacity: backend.themeFound ? 1.0 : 0.55
                            Behavior on opacity { NumberAnimation { duration: 140 } }
                            ScrollBar.horizontal: ScrollBar {}

                            delegate: Rectangle {
                                id: presetItem
                                required property var modelData
                                readonly property bool chosen: page.presetId === modelData.id

                                width: 176
                                height: presetList.height - 10
                                radius: 10
                                color: chosen ? Theme.accentSoft : Theme.raised
                                border.width: 1
                                border.color: chosen ? Theme.accent : Theme.border

                                Behavior on color { ColorAnimation { duration: 120 } }

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 9
                                    spacing: 5

                                    Row {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        Repeater {
                                            model: presetItem.modelData.preview
                                            delegate: Rectangle {
                                                required property var modelData
                                                width: 18
                                                height: 12
                                                radius: 3
                                                color: modelData
                                                border.width: 1
                                                border.color: Theme.border
                                            }
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        text: presetItem.modelData.name
                                        color: Theme.text
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        text: presetItem.modelData.desc
                                        color: Theme.textDim
                                        font.pixelSize: 11
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        // one click is enough: if nothing is
                                        // loaded yet, load it first
                                        if (!backend.themeFound)
                                            page.reload();
                                        if (!backend.themeFound)
                                            return;   // the log says why
                                        page.presetId = presetItem.modelData.id;
                                        backend.applyPreset(presetItem.modelData.id);
                                    }
                                }
                            }
                        }
                    }
                }

                // ---- toolbar ----
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    TextField {
                        id: filterField
                        Layout.preferredWidth: 240
                        placeholderText: qsTr("search every color")
                        text: page.filterText
                        color: Theme.text
                        placeholderTextColor: Theme.textDim
                        font.pixelSize: 12
                        selectByMouse: true
                        onTextChanged: page.filterText = text
                        background: Rectangle {
                            color: Theme.raised
                            border.color: filterField.activeFocus ? Theme.accent : Theme.border
                            radius: 8
                        }
                    }
                    ActionButton {
                        visible: page.filterText !== ""
                        text: qsTr("\u2715")
                        onClicked: {
                            page.filterText = "";
                            filterField.text = "";
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: qsTr("Original: %1 colors   Edited: %2%3")
                              .arg(backend.themeEntries.length)
                              .arg(page.editedCount)
                              .arg(page.filterText === "" ? ""
                                                           : qsTr("   Shown: %1")
                                                             .arg(page.filteredEntries.length))
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                    ActionButton {
                        text: qsTr("Clear Edits")
                        enabled: page.editedCount > 0
                        onClicked: {
                            page.presetId = "";
                            backend.clearThemeEdits();
                        }
                    }
                    ActionButton {
                        accent: true
                        text: qsTr("Patch Theme")
                        enabled: page.editedCount > 0
                        onClicked: {
                            const r = backend.patchThemeQml();
                            if (r.ok)
                                page.presetId = "";
                        }
                    }
                }

                // ---- column captions ----
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    spacing: 10
                    Text {
                        Layout.preferredWidth: 190
                        text: qsTr("Color")
                        color: Theme.textDim
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.preferredWidth: 78
                        text: qsTr("In EXE")
                        color: Theme.textDim
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Controls")
                        color: Theme.textDim
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.preferredWidth: 132
                        text: qsTr("New value")
                        color: Theme.textDim
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }

                // ---- color list ----
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ListView {
                        id: qmlList
                        anchors.fill: parent
                        clip: true
                        model: page.filteredEntries
                        spacing: 4
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}

                        delegate: Rectangle {
                            required property var modelData
                            width: qmlList.width
                            height: 40
                            radius: 8
                            color: rowArea.containsMouse ? Theme.raised : Theme.card
                            border.width: 1
                            border.color: modelData.edit !== "" ? Theme.accent : Theme.border

                            Behavior on color { ColorAnimation { duration: 90 } }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 10

                                Text {
                                    Layout.preferredWidth: 190
                                    elide: Text.ElideRight
                                    text: modelData.name
                                    color: Theme.text
                                    font.pixelSize: 12
                                    font.family: "Consolas"
                                }
                                Rectangle {
                                    Layout.preferredWidth: 16
                                    Layout.preferredHeight: 16
                                    radius: 4
                                    color: /^#[0-9A-Fa-f]{6,8}$/.test(modelData.value)
                                           ? (modelData.value.length === 9
                                              ? "#" + modelData.value.substring(3)
                                              : modelData.value)
                                           : Theme.raised
                                    border.width: 1
                                    border.color: Theme.border
                                }
                                Text {
                                    Layout.preferredWidth: 52
                                    text: modelData.value
                                    color: Theme.textDim
                                    font.pixelSize: 11
                                    font.family: "Consolas"
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: modelData.purpose
                                    color: Theme.textDim
                                    font.pixelSize: 11
                                }
                                ColorField {
                                    Layout.preferredWidth: 132
                                    current: modelData.value
                                    value: modelData.edit
                                    onEdited: (hex) => backend.setThemeEdit(modelData.name, hex)
                                }
                            }

                            MouseArea {
                                id: rowArea
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                                cursorShape: Qt.ArrowCursor
                            }
                        }
                    }

                    Column {
                        anchors.centerIn: parent
                        spacing: 6
                        visible: qmlList.count === 0
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: !backend.themeFound
                                  ? qsTr("Select HD-Player.exe, then choose a preset or edit a color.")
                                  : qsTr("No colors loaded/matched")
                            color: Theme.textDim
                            font.pixelSize: 13
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: page.filterText !== ""
                            text: qsTr("Clear the search to see all %1 colors.")
                                  .arg(backend.themeEntries.length)
                            color: Theme.textDim
                            font.pixelSize: 11
                        }
                    }
                }
            }

            // ---------------- Hex (legacy) method ----------------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: qsTr("Legacy palette - replaces color constants directly in the "
                                   + "binary. Use it only on builds where the QML method finds "
                                   + "nothing.")
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                    ActionButton {
                        text: qsTr("Reset All")
                        onClicked: backend.resetHexColors()
                    }
                    ActionButton {
                        text: qsTr("Preview")
                        onClicked: backend.previewHex()
                    }
                    ActionButton {
                        accent: true
                        text: qsTr("Apply Theme")
                        onClicked: backend.applyHex()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    spacing: 10
                    Item { Layout.preferredWidth: 18 }
                    Text {
                        Layout.preferredWidth: 190
                        text: qsTr("Color")
                        color: Theme.textDim
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.preferredWidth: 78
                        text: qsTr("In EXE")
                        color: Theme.textDim
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Controls")
                        color: Theme.textDim
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.preferredWidth: 132
                        text: qsTr("New value")
                        color: Theme.textDim
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ListView {
                        id: hexList
                        anchors.fill: parent
                        clip: true
                        model: backend.hexColors
                        spacing: 4
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}

                        delegate: Rectangle {
                            required property var modelData
                            width: hexList.width
                            height: 40
                            radius: 8
                            color: hexRowArea.containsMouse ? Theme.raised : Theme.card
                            border.width: 1
                            border.color: modelData.enabled ? Theme.accent : Theme.border

                            Behavior on color { ColorAnimation { duration: 90 } }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 10

                                Rectangle {
                                    Layout.preferredWidth: 18
                                    Layout.preferredHeight: 18
                                    radius: 5
                                    color: modelData.enabled ? Theme.accent : "transparent"
                                    border.width: 1
                                    border.color: modelData.enabled ? Theme.accent : Theme.border
                                    Text {
                                        anchors.centerIn: parent
                                        visible: modelData.enabled
                                        text: "\u2713"
                                        color: "#ffffff"
                                        font.pixelSize: 11
                                        font.weight: Font.Black
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: backend.setHexEnabled(modelData.name,
                                                                         !modelData.enabled)
                                    }
                                }

                                Text {
                                    Layout.preferredWidth: 190
                                    elide: Text.ElideRight
                                    text: modelData.name
                                    color: Theme.text
                                    font.pixelSize: 12
                                    font.family: "Consolas"
                                }
                                Rectangle {
                                    Layout.preferredWidth: 16
                                    Layout.preferredHeight: 16
                                    radius: 4
                                    color: /^#[0-9A-Fa-f]{6,8}$/.test(modelData.current)
                                           ? (modelData.current.length === 9
                                              ? "#" + modelData.current.substring(3)
                                              : modelData.current)
                                           : Theme.raised
                                    border.width: 1
                                    border.color: Theme.border
                                }
                                Text {
                                    Layout.preferredWidth: 52
                                    text: modelData.current
                                    color: Theme.textDim
                                    font.pixelSize: 11
                                    font.family: "Consolas"
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: modelData.purpose
                                    color: Theme.textDim
                                    font.pixelSize: 11
                                }
                                ColorField {
                                    Layout.preferredWidth: 132
                                    current: modelData.current
                                    value: modelData.color !== modelData.current
                                           ? modelData.color : ""
                                    onEdited: (hex) => backend.setHexColor(modelData.name, hex)
                                }
                            }

                            MouseArea {
                                id: hexRowArea
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: hexList.count === 0
                        text: qsTr("No colors loaded/matched")
                        color: Theme.textDim
                        font.pixelSize: 13
                    }
                }
            }
        }
    }
}
