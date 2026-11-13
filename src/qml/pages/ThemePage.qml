import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import BSICustomizer

Item {
    id: page

    // Which pane is shown comes from the method the backend actually loaded
    // (after auto-detection resolved it), so "Auto", a manual "QML (new)" pick
    // and a manual "Hex (old)" pick all display exactly what was loaded.
    property bool qmlMethod: backend.resolvedThemeMethod === "qml"
    property bool fileChanged: backend.themeFileChanged()
    property bool showPreview: false
    property string diagnosticStatus: ""
    property string filterText: ""
    property string presetId: ""
    property string hexPresetId: ""

    GradientEditor {
        id: gradientEditor
        pendingSolidEdits: page.editedCount > 0
    }

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

    // The method chips are [Auto, QML (new), Hex (old)]. Auto (the default)
    // lets the backend detect from the EXE's Qt imports whether this player
    // supports the QML method or needs the legacy Hex method, and the page
    // follows whatever was resolved.
    function reload() {
        const methods = ["auto", "qml", "hex"];
        // Loading re-reads the EXE and drops every staged edit; the preset
        // selection describes those edits, so it goes with them.
        page.presetId = "";
        page.hexPresetId = "";
        backend.loadTheme(methods[Math.max(0, Math.min(2, methodChips.currentIndex))]);
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
        // An EXE picked on the Dashboard (or restored from settings) can need
        // the other method: re-arm full auto-detection for the next load.
        function onExePathChanged() {
            methodChips.currentIndex = 0;
            page.presetId = "";
            page.hexPresetId = "";
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            ActionButton { text: page.showPreview ? qsTr("Hide preview") : qsTr("Preview palette"); onClicked: page.showPreview = !page.showPreview }
            ActionButton { text: qsTr("Dry Run"); enabled: backend.themeFound && !backend.busy; onClicked: { const r = backend.workspace("theme-dry-run", {}); page.diagnosticStatus = r.msg || ""; } }
            ActionButton { text: qsTr("Export Diagnostics…"); enabled: !backend.busy; onClicked: diagnosticsDialog.open() }
            Label { Layout.fillWidth: true; color: Theme.textDim; wrapMode: Text.Wrap; text: page.diagnosticStatus }
        }
        ThemePreview { Layout.fillWidth: true; visible: page.showPreview; entries: backend.themeEntries }
        Label {
            Layout.fillWidth: true; visible: backend.themeFound && !backend.themeWritable
            color: Theme.logOrange; wrapMode: Text.Wrap
            text: qsTr("Read-only theme: the active resource mapping is not verified or is ambiguous. Patching is disabled to protect the EXE. Export Diagnostics; forcing Hex is not a safe workaround.")
        }
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
                    visible: backend.themeDetection.length > 0
                    text: backend.themeDetection
                    color: Theme.textDim
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
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
                // Always visible: Auto is the full auto-detect default, and the
                // two concrete methods stay available as an informed override.
                // Forcing QML on a Qt 5 player is explained by the status line
                // instead of hiding the chooser.
                model: [qsTr("Auto"), qsTr("QML (new)"), qsTr("Hex (old)")]
                onSelected: (index) => {
                    page.presetId = "";
                    page.hexPresetId = "";
                    backend.loadTheme(index === 0 ? "auto" : index === 1 ? "qml" : "hex");
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
                // A different EXE can support a different method: go back to
                // full auto-detection so the chooser never stays stuck on a
                // manually picked method that this player cannot use.
                methodChips.currentIndex = 0;
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
                        text: qsTr("Gradients")
                        enabled: page.qmlMethod && backend.themeFound
                        onClicked: {
                            gradientEditor.hexMode = false;
                            gradientEditor.open();
                        }
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
                        enabled: backend.themeWritable && backend.themeMode !== "hex" && !backend.busy && !page.fileChanged
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

                // ---- presets (same palettes as the QML editor, staged as
                //      legacy color values instead of QML source edits) ----
                Card {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 146

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 6

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
                                      ? qsTr("Load the Hex (old) theme first - then pick a palette.")
                                      : (backend.hexColors.length === 0
                                         ? qsTr("This player exposes no legacy palette names - "
                                                + "use Gradient theme for its surfaces.")
                                         : (page.hexPresetId === ""
                                            ? qsTr("Pick a palette to fill the New value column, "
                                                   + "then Apply Theme.")
                                            : qsTr("Preset staged - press Apply Theme to write it.")))
                                color: Theme.textDim
                                font.pixelSize: 12
                            }
                            ActionButton {
                                text: qsTr("Clear")
                                onClicked: {
                                    page.hexPresetId = "";
                                    backend.resetHexColors();
                                }
                            }
                            ActionButton {
                                text: qsTr("Recolor switches")
                                enabled: page.hexPresetId !== ""
                                onClicked: backend.recolorTogglesFromPreset(page.hexPresetId)
                            }
                        }

                        ListView {
                            id: hexPresetList
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
                                id: hexPresetItem
                                required property var modelData
                                readonly property bool chosen: page.hexPresetId === modelData.id

                                width: 168
                                height: hexPresetList.height - 4
                                radius: 10
                                color: chosen ? Theme.accentSoft : Theme.raised
                                border.width: 1
                                border.color: chosen ? Theme.accent : Theme.border

                                Behavior on color { ColorAnimation { duration: 120 } }

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 7
                                    spacing: 2

                                    Row {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        Repeater {
                                            model: hexPresetItem.modelData.preview
                                            delegate: Rectangle {
                                                required property var modelData
                                                width: 15
                                                height: 10
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
                                        text: hexPresetItem.modelData.name
                                        color: Theme.text
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        text: hexPresetItem.modelData.desc
                                        color: Theme.textDim
                                        font.pixelSize: 11
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (!backend.themeFound)
                                            page.reload();
                                        // A reload can resolve to QML (new) and
                                        // hide this pane; only stage when the
                                        // Hex method is still the active one.
                                        if (!backend.themeFound
                                                || backend.themeMode !== "hex")
                                            return;
                                        if (backend.hexColors.length === 0) {
                                            // Reports the reason in the log.
                                            backend.applyHexPreset(hexPresetItem.modelData.id);
                                            return;
                                        }
                                        page.hexPresetId = hexPresetItem.modelData.id;
                                        backend.applyHexPreset(hexPresetItem.modelData.id);
                                    }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: qsTr("Legacy palette - replaces color constants directly in the "
                                   + "binary. Pick a preset above or set individual colors. "
                                   + "Gradient theme paints the top bar / title bar, "
                                   + "the sidebar and the settings panel with custom stops you "
                                   + "choose, one surface or all at once; "
                                   + "everything else uses the solid colors below.")
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                    ActionButton {
                        text: qsTr("Gradient theme")
                        enabled: backend.exePath !== ""
                        onClicked: {
                            gradientEditor.hexMode = true;
                            gradientEditor.open();
                        }
                    }
                    ActionButton {
                        text: qsTr("Reset All")
                        onClicked: {
                            page.hexPresetId = "";
                            backend.resetHexColors();
                        }
                    }
                    ActionButton {
                        text: qsTr("Preview")
                        onClicked: backend.previewHex()
                    }
                    ActionButton {
                        accent: true
                        text: qsTr("Apply Theme")
                        onClicked: {
                            const r = backend.applyHex();
                            if (r.ok)
                                page.hexPresetId = "";
                        }
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
    FileDialog {
        id: diagnosticsDialog; fileMode: FileDialog.SaveFile; defaultSuffix: "txt"; nameFilters: ["Text reports (*.txt)"]
        onAccepted: { const r = backend.workspace("diagnostics", {file: backend.localFilePath(selectedFile)}); page.diagnosticStatus = r.msg || ""; }
    }

}
