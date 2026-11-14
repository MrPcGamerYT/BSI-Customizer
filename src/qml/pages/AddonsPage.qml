import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import BSICustomizer
import "../SkinCatalog.js" as Catalog
import "../Icons.js" as Icons

Item {
    id: page
    signal notify(string message)
    signal navigate(int index)
    property int selected: 0
    property var draft: Catalog.create(0)
    property var drafts: ({})
    property var plan: ({})
    property string planPath: ""
    property string paletteJson: "{}"
    property string tokenJson: "{}"
    property string mappingJson: "[]"
    property var inspected: []
    property string customSvg: ""
    readonly property bool wide: width >= 1010
    // Scan results are consumed only while the user opens the advanced editor.
    readonly property var svgResources: editor.opened ? backend.resourceInfos.filter(x => x.path.toLowerCase().endsWith(".svg")) : []
    readonly property color previewAccent: /^#[0-9a-fA-F]{6}$/.test(draft.accent) ? draft.accent : "#7c5cff"
    readonly property color previewSurface: /^#[0-9a-fA-F]{6}$/.test(draft.surface) ? draft.surface : "#101012"
    function edit(key, value) { var next = Object.assign({}, draft); next[key] = value; draft = next; plan = {}; }
    function selectPack(index) {
        // Keep each pack's independent edits, including invalid JSON in progress.
        var saved = Object.assign({}, drafts);
        saved[selected] = {draft: draft, palette: paletteJson, tokens: tokenJson, icons: mappingJson}; drafts = saved;
        selected = index;
        const existing = drafts[index];
        draft = existing ? existing.draft : Catalog.create(index);
        paletteJson = existing ? existing.palette : "{}"; tokenJson = existing ? existing.tokens : "{}";
        mappingJson = existing ? existing.icons : "[]"; plan = {};
    }
    function profile() {
        var result = Object.assign({}, draft);
        result.palette = JSON.parse(paletteJson); result.tokens = JSON.parse(tokenJson); result.icons = JSON.parse(mappingJson);
        return result;
    }
    function run(action, options) {
        const r = backend.workspace(action, options || {}); page.notify(r.msg || ""); return r;
    }
    function openEditor() { editor.open(); }
    function closeEditor() { editor.close(); }
    function previewSelected() { playerPreview.replay(); }
    function stage() {
        plan = {};
        try { run("queue-skin", { profile: profile() }); }
        catch (e) { page.notify(qsTr("Invalid profile JSON: ") + String(e)); }
    }
    function verify() {
        plan = {}; const r = page.run("dry-run");
        if (r.ok) { page.planPath = backend.exePath; page.plan = r; }
    }
    function loadProfile(p) {
        draft = p; paletteJson = JSON.stringify(p.palette || {}, null, 2);
        tokenJson = JSON.stringify(p.tokens || {}, null, 2); mappingJson = JSON.stringify(p.icons || [], null, 2);
        selected = Catalog.packs.findIndex(x => x.id === p.base); plan = {};
    }
    function addMapping() {
        try {
            const target = page.svgResources[iconTarget.currentIndex];
            if (!target) { page.notify(qsTr("Scan Resources manually first, then select an SVG target.")); return; }
            var icons = JSON.parse(page.mappingJson).filter(x => x.path !== target.path);
            icons.push(page.customSvg.length ? {path: target.path, svg: page.customSvg} : {path: target.path, glyph: iconGlyph.currentText});
            page.mappingJson = JSON.stringify(icons, null, 2); page.plan = {};
        } catch (e) { page.notify(String(e)); }
    }
    Connections {
        target: backend
        function onWorkspaceChanged() { page.plan = {}; }
        function onExePathChanged() { page.plan = {}; page.inspected = []; }
    }
    ScrollView {
        id: scroll; anchors.fill: parent; contentWidth: availableWidth; clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            width: scroll.availableWidth; spacing: 26
            ColumnLayout {
                Layout.fillWidth: true; spacing: 12
                RowLayout {
                    spacing: 8
                    Rectangle { width: 11; height: 11; radius: 6; color: "transparent"; border.color: Theme.tertiary
                        Rectangle { width: 3; height: 3; radius: 2; anchors.centerIn: parent; color: Theme.tertiary }
                    }
                    MicroLabel { text: qsTr("CONTROL CENTER / COLLECTION") }
                }
                Label { text: qsTr("Make it yours."); color: Theme.text; font.family: Theme.headingFont; font.pixelSize: 40; font.weight: Font.DemiBold; font.letterSpacing: -1.8 }
            }
            GridLayout {
                Layout.fillWidth: true; columns: page.wide ? 2 : 1; columnSpacing: 28; rowSpacing: 26
                ColumnLayout {
                    Layout.fillWidth: true; Layout.alignment: Qt.AlignTop; spacing: 20
                    GridLayout {
                        objectName: "collectionBento"
                        Layout.fillWidth: true; columns: 3; columnSpacing: 14; rowSpacing: 14
                        Repeater {
                            model: Catalog.packs
                            CollectionCard {
                                required property var modelData
                                required property int index
                                objectName: "skinCollection" + index
                                Layout.fillWidth: true; Layout.preferredHeight: 176
                                feature: index === 0; title: modelData.name; tint: modelData.tint; selected: page.selected === index
                                onPicked: page.selectPack(index)
                                onPlayed: { page.selectPack(index); playerPreview.replay(); }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 14
                        Rectangle {
                            Layout.fillWidth: true; Layout.preferredWidth: 1.15; Layout.preferredHeight: 194
                            color: "#0b0b10"; radius: 16; border.color: Theme.border
                            MicroLabel { x: 18; y: 15; text: qsTr("LIVE CANVAS"); font.pixelSize: 8; color: Theme.tertiary }
                            Item {
                                anchors.fill: parent; anchors.margins: 18; anchors.topMargin: 39; anchors.bottomMargin: 35
                                FrameCorners { anchors.fill: parent; tint: "#3a3946" }
                                SkinPreview {
                                    id: playerPreview; objectName: "nativeSkinPreview"
                                    anchors.centerIn: parent; width: parent.width-34; height: parent.height-18
                                    packName: page.draft.name; accentColor: page.previewAccent; surfaceColor: page.previewSurface
                                    cornerRadius: page.draft.radius; controlHeight: page.draft.height
                                    duration: page.draft.duration; motion: page.draft.motion; iconStyle: page.draft.iconStyle
                                }
                            }
                            Rectangle { x: 1; y: parent.height-29; width: parent.width-2; height: 1; color: Theme.border }
                            MicroLabel { x: 17; y: parent.height-20; text: "LQ"; color: Theme.tertiary; font.pixelSize: 8 }
                            MicroLabel { anchors.horizontalCenter: parent.horizontalCenter; y: parent.height-20; text: "1 / 1"; color: Theme.tertiary; font.pixelSize: 8 }
                            CircleButton { width: 24; height: 24; anchors.right: parent.right; anchors.rightMargin: 12; y: parent.height-27; filled: true; text: qsTr("Replay preview"); onClicked: playerPreview.replay() }
                        }
                        Rectangle {
                            Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.preferredHeight: 194
                            color: "#050507"; radius: 16; border.color: Theme.border
                            Column {
                                x: 22; y: 32; width: parent.width-44; spacing: 13
                                Text { text: "VOiD"; color: "#f2f2f5"; font.family: Theme.headingFont; font.pixelSize: 31; font.weight: Font.DemiBold; font.letterSpacing: 8 }
                                Label { text: "Developed By NOVA XIS"; color: "#aaaab5"; font.pixelSize: 10 }
                                MicroLabel { width: parent.width; text: "YOUTUBE / @NOVA_XIS20"; color: "#5c5c68"; font.pixelSize: 8; font.letterSpacing: .5 }
                            }
                            ActionButton { anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 22; implicitWidth: 59; implicitHeight: 24; text: "▶"; accent: true; Accessible.name: qsTr("Open NOVA XIS YouTube"); onClicked: Qt.openUrlExternally("https://www.youtube.com/@nova_xis20") }
                        }
                    }
                }
                Rectangle {
                    objectName: "selectedDesignInspector"
                    Layout.preferredWidth: page.wide ? 280 : -1; Layout.fillWidth: !page.wide; Layout.alignment: Qt.AlignTop
                    implicitHeight: inspector.implicitHeight + 40; color: Theme.card; radius: 18; border.color: Theme.border
                    ColumnLayout {
                        id: inspector; x: 20; y: 20; width: parent.width-40; spacing: 22
                        RowLayout {
                            Image { source: Icons.icon("<path d='M20 15A9 9 0 0 1 9 4a9 9 0 1 0 11 11z'/>", Theme.textDim); sourceSize: Qt.size(14,14); Layout.preferredWidth: 14; Layout.preferredHeight: 14 }
                            MicroLabel { text: qsTr("SELECTED DESIGN"); Layout.fillWidth: true }
                        }
                        RowLayout {
                            spacing: 10
                            Rectangle { width: 12; height: 12; radius: 6; color: "transparent"; border.color: Theme.text
                                Rectangle { width: 4; height: 4; radius: 2; anchors.centerIn: parent; color: Theme.text }
                            }
                            Label { objectName: "selectedDesignName"; Layout.fillWidth: true; text: page.draft.name; textFormat: Text.PlainText; elide: Text.ElideRight; font.pixelSize: 14; font.weight: Font.DemiBold; color: Theme.text }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 0
                            Repeater {
                                model: [ [qsTr("CORNERS"), page.draft.radius + "px"], [qsTr("HEIGHT"), page.draft.height + "px"], [qsTr("PADDING"),page.draft.padding + "px / " + page.draft.padding + "px"], [qsTr("BORDERS"),qsTr("Hairline 1px")], [qsTr("MOTION"),qsTr("Preview only")] ]
                                ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true; spacing: 0
                                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                                    RowLayout {
                                        Layout.fillWidth: true; Layout.preferredHeight: 41
                                        MicroLabel { text: modelData[0]; font.pixelSize: 9; color: Theme.tertiary; Layout.fillWidth: true }
                                        MicroLabel { text: modelData[1]; font.pixelSize: 9; font.letterSpacing: 0; font.capitalization: Font.MixedCase; color: Theme.textDim }
                                    }
                                }
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 9
                            ActionButton { objectName: "stageSkin"; Layout.fillWidth: true; implicitHeight: 40; text: qsTr("1. Stage this pack"); accent: true; enabled: !backend.busy; onClicked: page.stage() }
                            ActionButton { objectName: "verifySkin"; Layout.fillWidth: true; implicitHeight: 40; text: qsTr("2. Verify Dry Run"); enabled: !backend.busy && backend.workspaceOperations.length > 0; onClicked: page.verify() }
                            ActionButton { objectName: "applySkin"; Layout.fillWidth: true; implicitHeight: 40; text: qsTr("3. Review & apply"); enabled: !backend.busy && !!page.plan.ok && page.planPath === backend.exePath; onClicked: applyDialog.open() }
                            Label { Layout.fillWidth: true; Layout.topMargin: 3; text: qsTr("Supported palette and icon geometry only.\nAnimations are preview-only."); horizontalAlignment: Text.AlignHCenter; color: Theme.tertiary; font.pixelSize: 10; wrapMode: Text.Wrap; lineHeight: 1.5 }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 12
                            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                            ActionButton { objectName: "customizeSkin"; Layout.fillWidth: true; text: qsTr("Customize design  ↗"); onClicked: editor.open() }
                            RowLayout {
                                Layout.fillWidth: true
                                ActionButton { Layout.fillWidth: true; text: qsTr("Import"); onClicked: openProfile.open() }
                                ActionButton { Layout.fillWidth: true; text: qsTr("Save as"); onClicked: saveProfile.open() }
                            }
                        }
                    }
                }
            }
        }
    }
    Drawer {
        id: editor; objectName: "skinEditor"; edge: Qt.RightEdge
        width: Math.min(760,page.width); height: parent ? parent.height : page.height
        modal: true; dim: true; padding: 24
        background: Rectangle { color: Theme.card; border.color: Theme.border; radius: 20 }
        contentItem: ScrollView {
            id: editorScroll; contentWidth: availableWidth; clip: true
            ColumnLayout {
                width: editorScroll.availableWidth; spacing: 18
                RowLayout {
                    Layout.fillWidth: true
                    Label { Layout.fillWidth: true; text: qsTr("Your design, in detail."); font.family: Theme.headingFont; font.pixelSize: 24; font.weight: Font.DemiBold; color: Theme.text }
                    CircleButton { text: qsTr("Close editor"); glyph: "<path d='m6 6 12 12M6 18 18 6'/>"; onClicked: editor.close() }
                }
                MicroLabel { text: qsTr("EDITABLE / NOT A LOCKED PRESET") }
                    GridLayout {
                        Layout.fillWidth: true; columns: editorScroll.availableWidth < 720 ? 2 : 4; columnSpacing: 14; rowSpacing: 10
                        ColumnLayout { Layout.fillWidth: true; Label { text: qsTr("Pack name"); color: Theme.textDim }
                            TextField { objectName: "skinPackName"; Layout.fillWidth: true; text: page.draft.name; maximumLength: 80; onTextEdited: page.edit("name", text) }
                        }
                        ColumnLayout { Layout.fillWidth: true; Label { text: qsTr("Accent / icons (#RRGGBB)"); color: Theme.textDim }
                            TextField { objectName: "skinAccent"; Layout.fillWidth: true; text: page.draft.accent; maximumLength: 7; onTextEdited: page.edit("accent", text) }
                        }
                        ColumnLayout { Layout.fillWidth: true; Label { text: qsTr("Player surface (#RRGGBB)"); color: Theme.textDim }
                            TextField { Layout.fillWidth: true; text: page.draft.surface; maximumLength: 7; onTextEdited: page.edit("surface", text) }
                        }
                        ColumnLayout { Layout.fillWidth: true; Label { text: qsTr("Icon family"); color: Theme.textDim }
                            ComboBox { Layout.fillWidth: true; model: ["line", "rounded", "bold"]; currentIndex: model.indexOf(page.draft.iconStyle); onActivated: page.edit("iconStyle", currentText) }
                        }
                        ColumnLayout { Label { text: qsTr("Button corners"); color: Theme.textDim }
                            SpinBox { from: 0; to: 32; editable: true; value: page.draft.radius; onValueModified: page.edit("radius", value) }
                        }
                        ColumnLayout { Label { text: qsTr("Button height"); color: Theme.textDim }
                            SpinBox { from: 16; to: 96; editable: true; value: page.draft.height; onValueModified: page.edit("height", value) }
                        }
                        ColumnLayout { Label { text: qsTr("Horizontal padding"); color: Theme.textDim }
                            SpinBox { from: 0; to: 64; editable: true; value: page.draft.padding; onValueModified: page.edit("padding", value) }
                        }
                        ColumnLayout { Label { text: qsTr("Preview motion"); color: Theme.textDim }
                            ComboBox { model: ["none", "fade", "rise", "slide", "scale"]; currentIndex: model.indexOf(page.draft.motion); onActivated: page.edit("motion", currentText) }
                        }
                    }
                    RowLayout {
                        Label { text: qsTr("Preview duration (ms)"); color: Theme.textDim }
                        SpinBox { from: 0; to: 1200; stepSize: 20; editable: true; value: page.draft.duration; onValueModified: page.edit("duration", value) }
                        CheckBox { text: qsTr("Reduce app motion"); checked: Theme.reduceMotion; onToggled: Theme.reduceMotion = checked }
                        ActionButton { text: qsTr("Replay"); onClicked: playerPreview.replay() }
                    }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.warn; text: qsTr("Player deployment currently supports mapped palette, safe numeric design tokens and explicitly mapped SVG icons. Motion and pack name are authoring/preview metadata, not injected player animations or branding. Full-window skins, native dialogs, optimizer and boot are not covered.") }

                    Label { text: qsTr("Advanced / no locked preset values"); color: Theme.text; font.pixelSize: 16; font.bold: true }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textDim; text: qsTr("Override any detected palette property or supported design token. JSON imports are data only: scripts, raw offsets and unknown properties are refused. Inspect is manual; icons require a separate manual Scan Resources.") }
                    RowLayout {
                        ActionButton { text: qsTr("Inspect design tokens"); enabled: !backend.busy; onClicked: { const r = page.run("appearance-inspect"); page.inspected = r.tokens || []; } }
                        ActionButton { text: qsTr("Color studio →"); onClicked: page.navigate(1) }
                        ActionButton { text: qsTr("Resources →"); onClicked: page.navigate(2) }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ActionButton { text: qsTr("Load target palette"); enabled: !backend.busy; onClicked: backend.loadTheme("qml") }
                        ComboBox { id: paletteTarget; Layout.fillWidth: true; model: backend.themeEntries; textRole: "name" }
                        TextField { id: paletteValue; Layout.preferredWidth: 100; placeholderText: "#RRGGBB"; maximumLength: 7 }
                        ActionButton { text: qsTr("Set color"); enabled: paletteTarget.currentIndex >= 0; onClicked: { try { var colors = JSON.parse(page.paletteJson); colors[backend.themeEntries[paletteTarget.currentIndex].name] = paletteValue.text; page.paletteJson = JSON.stringify(colors,null,2); page.plan = {}; } catch (e) { page.notify(String(e)); } } }
                    }
                    Label { text: qsTr("Exact palette-name → #RRGGBB overrides"); color: Theme.textDim }
                    TextArea { objectName: "skinPaletteOverrides"; Layout.fillWidth: true; Layout.preferredHeight: 80; text: page.paletteJson; font.family: Theme.monoFont; color: Theme.text; selectByMouse: true; wrapMode: TextEdit.Wrap; onTextChanged: if (activeFocus) { page.paletteJson = text; page.plan = {}; } }
                    Label { text: qsTr("Exact design-token key → integer overrides"); color: Theme.textDim }
                    TextArea { Layout.fillWidth: true; Layout.preferredHeight: 80; text: page.tokenJson; font.family: Theme.monoFont; color: Theme.text; selectByMouse: true; wrapMode: TextEdit.Wrap; onTextChanged: if (activeFocus) { page.tokenJson = text; page.plan = {}; } }
                    TextArea { Layout.fillWidth: true; Layout.preferredHeight: page.inspected.length ? 130 : 0; visible: page.inspected.length > 0; readOnly: true; selectByMouse: true; color: Theme.textDim; text: JSON.stringify(page.inspected, null, 2) }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("SVG mappings: [{\"path\":\"exact/scanned/resource.svg\",\"glyph\":\"home\"}]. Glyphs: home, play, settings, volume, controls, capture, fullscreen, back, apps. Icons use the chosen family and accent. Verify your target mapping; replacing an image does not add a functional sidebar button."); color: Theme.textDim }
                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox { id: iconTarget; Layout.fillWidth: true; model: page.svgResources; textRole: "path" }
                        ComboBox { id: iconGlyph; model: ["home", "play", "settings", "volume", "controls", "capture", "fullscreen", "back", "apps"] }
                        ActionButton { text: qsTr("Custom SVG…"); enabled: !backend.busy; onClicked: openIcon.open() }
                        ActionButton { text: page.customSvg.length ? qsTr("Map custom SVG") : qsTr("Map icon"); enabled: page.svgResources.length > 0; onClicked: page.addMapping() }
                    }
                    RowLayout {
                        visible: page.customSvg.length > 0
                        Image { Layout.preferredWidth: 28; Layout.preferredHeight: 28; source: page.customSvg.length ? "data:image/svg+xml;base64," + page.customSvg : "" }
                        Label { text: qsTr("Custom static SVG ready. It retains its own colors."); color: Theme.textDim }
                        ActionButton { text: qsTr("Use built-in icons"); onClicked: page.customSvg = "" }
                    }
                    TextArea { objectName: "skinIconMappings"; Layout.fillWidth: true; Layout.preferredHeight: 80; text: page.mappingJson; color: Theme.text; font.family: Theme.monoFont; selectByMouse: true; wrapMode: TextEdit.Wrap; onTextChanged: if (activeFocus) { page.mappingJson = text; page.plan = {}; } }
                Item { Layout.preferredHeight: 10 }
            }
        }
    }
    FileDialog { id: openIcon; nameFilters: ["Static SVG icons (*.svg)"]; onAccepted: { const r = page.run("skin-icon-load", {file: backend.localFilePath(selectedFile)}); if (r.ok) page.customSvg = r.svg; } }
    FileDialog { id: openProfile; nameFilters: ["Skin profiles (*.json)"]; onAccepted: { const r = page.run("skin-load", {file: backend.localFilePath(selectedFile)}); if (r.ok) page.loadProfile(r.profile); } }
    FileDialog { id: saveProfile; fileMode: FileDialog.SaveFile; defaultSuffix: "json"; nameFilters: ["Skin profiles (*.json)"]; onAccepted: { try { page.run("skin-save", {file: backend.localFilePath(selectedFile), profile: page.profile()}); } catch (e) { page.notify(String(e)); } } }
    Dialog {
        id: applyDialog; width: Math.min(660,page.width-32); implicitHeight: 400; anchors.centerIn: parent; modal: true
        title: qsTr("Review verified skin batch"); standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: ScrollView {
            clip: true; contentWidth: availableWidth
            TextArea { readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; color: Theme.textDim
                text: qsTr("Close every emulator instance first. Only the verified queued operations below will be applied. A recovery snapshot is retained.\n\n") + JSON.stringify(page.plan,null,2)
            }
        }
        onAccepted: { if (page.plan.ok && page.planPath === backend.exePath) { const hash = page.plan.outputSha256; page.plan = {}; page.run("apply", {outputSha256: hash}); } }
    }
}
