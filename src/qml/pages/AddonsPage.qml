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
    property var plan: ({})
    property string planPath: ""
    property string paletteJson: "{}"
    property string tokenJson: "{}"
    property var inspected: []
    readonly property var svgResources: backend.resourceInfos.filter(x => x.path.toLowerCase().endsWith(".svg"))
    function addMapping() {
        try {
            const target = page.svgResources[iconTarget.currentIndex];
            if (!target) { page.notify(qsTr("Scan Resources manually first, then select an SVG target.")); return; }
            var icons = JSON.parse(page.mappingJson).filter(x => x.path !== target.path);
            icons.push({path: target.path, glyph: iconGlyph.currentText});
            page.mappingJson = JSON.stringify(icons, null, 2); page.plan = {};
        } catch (e) { page.notify(String(e)); }
    }
    property string mappingJson: "[]"
    readonly property color previewAccent: /^#[0-9a-fA-F]{6}$/.test(draft.accent) ? draft.accent : "#d8faec"
    readonly property color previewSurface: /^#[0-9a-fA-F]{6}$/.test(draft.surface) ? draft.surface : "#101012"
    function edit(key, value) { var next = Object.assign({}, draft); next[key] = value; draft = next; plan = {}; }
    function selectPack(index) {
        selected = index; draft = Catalog.create(index); paletteJson = "{}"; tokenJson = "{}"; mappingJson = "[]"; plan = {};
        playerPreview.replay();
    }
    function profile() {
        var result = Object.assign({}, draft);
        result.palette = JSON.parse(paletteJson); result.tokens = JSON.parse(tokenJson); result.icons = JSON.parse(mappingJson);
        return result;
    }
    function run(action, options) {
        const r = backend.workspace(action, options || {}); page.notify(r.msg || ""); return r;
    }
    function stage() {
        plan = {};
        try { run("queue-skin", { profile: profile() }); }
        catch (e) { page.notify(qsTr("Invalid profile JSON: ") + String(e)); }
    }
    function loadProfile(p) {
        draft = p; paletteJson = JSON.stringify(p.palette || {}, null, 2);
        tokenJson = JSON.stringify(p.tokens || {}, null, 2); mappingJson = JSON.stringify(p.icons || [], null, 2);
        selected = Catalog.packs.findIndex(x => x.id === p.base); plan = {};
    }
    Connections {
        target: backend
        function onWorkspaceChanged() { page.plan = {}; }
        function onExePathChanged() { page.plan = {}; page.inspected = []; }
    }
    ScrollView {
        id: scroll; anchors.fill: parent; contentWidth: availableWidth; clip: true
        ColumnLayout {
            width: scroll.availableWidth; spacing: 18
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 6
                    Label { text: qsTr("PLAYER COLLECTION / SKIN STUDIO"); color: Theme.textDim; font.pixelSize: 10; font.letterSpacing: 1.8 }
                    Label { text: qsTr("Make it yours."); color: Theme.text; font.family: Theme.headingFont; font.pixelSize: 30; font.bold: true }
                    Label { text: qsTr("One dark canvas. Your name, your colors, your character."); color: Theme.soft; font.pixelSize: 13 }
                }
                Label { text: qsTr("6 EDITABLE BASES"); color: Theme.accentText; font.pixelSize: 10; font.bold: true }
            }
            GridLayout {
                Layout.fillWidth: true; columns: scroll.availableWidth < 690 ? 2 : 3; columnSpacing: 12; rowSpacing: 12
                Repeater {
                    model: Catalog.packs
                    AbstractButton {
                        id: packCard
                        required property var modelData
                        required property int index
                        objectName: "skinCollection" + index
                        Layout.fillWidth: true; implicitHeight: 136
                        Accessible.name: modelData.name
                        onClicked: page.selectPack(index)
                        background: Rectangle {
                            radius: 17; color: packCard.modelData.tint
                            border.width: page.selected === packCard.index ? 2 : 1
                            border.color: page.selected === packCard.index ? "#f1f1f4" : Qt.alpha("white", 0.12)
                            Rectangle { anchors.fill: parent; radius: 17; color: packCard.hovered ? "#12ffffff" : "transparent" }
                            Behavior on scale { NumberAnimation { duration: Theme.motionDuration; easing.type: Easing.OutCubic } }
                            scale: packCard.down ? 0.98 : 1
                        }
                        contentItem: Item {
                            Label { x: 16; y: 12; text: "𝓥𝓞𝓘𝓓"; font.family: branding.name; color: "#ffffff"; font.pixelSize: 21 }
                            Rectangle {
                                anchors.right: parent.right; anchors.rightMargin: 28; anchors.top: parent.top; anchors.topMargin: 30
                                width: 28; height: 55; radius: 5; color: "#0b0b0e"; border.color: "#424248"
                                Image { anchors.centerIn: parent; width: 12; height: 12; source: Icons.icon(Icons.dashboard, packCard.modelData.accent) }
                                Rectangle { width: 20; height: 3; radius: 1; color: "#34343b"; anchors.bottom: parent.bottom; anchors.bottomMargin: 5; anchors.horizontalCenter: parent.horizontalCenter }
                            }
                            Label { anchors.left: parent.left; anchors.leftMargin: 16; anchors.bottom: parent.bottom; anchors.bottomMargin: 14; text: packCard.modelData.name; color: "#ffffff"; font.pixelSize: 12; font.weight: Font.DemiBold }
                            Label { anchors.right: parent.right; anchors.rightMargin: 13; anchors.bottom: parent.bottom; anchors.bottomMargin: 12; text: "▶"; color: "white"; font.pixelSize: 15 }
                        }
                    }
                }
            }
            Card {
                Layout.fillWidth: true; pad: 20
                ColumnLayout {
                    width: parent.width; spacing: 16
                    RowLayout {
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: qsTr("Selected design / fully editable profile"); color: Theme.text; font.pixelSize: 16; font.bold: true }
                        ActionButton { text: qsTr("Import…"); onClicked: openProfile.open() }
                        ActionButton { text: qsTr("Save as…"); onClicked: saveProfile.open() }
                    }
                    SkinPreview {
                        id: playerPreview; objectName: "nativeSkinPreview"
                        Layout.fillWidth: true; Layout.preferredHeight: 245
                        packName: page.draft.name; accentColor: page.previewAccent; surfaceColor: page.previewSurface
                        cornerRadius: page.draft.radius; controlHeight: page.draft.height
                        duration: page.draft.duration; motion: page.draft.motion; iconStyle: page.draft.iconStyle
                    }
                    GridLayout {
                        Layout.fillWidth: true; columns: scroll.availableWidth < 720 ? 2 : 4; columnSpacing: 14; rowSpacing: 10
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
                }
            }
            Card {
                Layout.fillWidth: true
                ColumnLayout {
                    width: parent.width; spacing: 10
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
                    TextArea { objectName: "skinPaletteOverrides"; Layout.fillWidth: true; Layout.preferredHeight: 80; text: page.paletteJson; font.family: "Consolas"; color: Theme.text; selectByMouse: true; wrapMode: TextEdit.Wrap; onTextChanged: if (activeFocus) { page.paletteJson = text; page.plan = {}; } }
                    Label { text: qsTr("Exact design-token key → integer overrides"); color: Theme.textDim }
                    TextArea { Layout.fillWidth: true; Layout.preferredHeight: 80; text: page.tokenJson; font.family: "Consolas"; color: Theme.text; selectByMouse: true; wrapMode: TextEdit.Wrap; onTextChanged: if (activeFocus) { page.tokenJson = text; page.plan = {}; } }
                    TextArea { Layout.fillWidth: true; Layout.preferredHeight: page.inspected.length ? 130 : 0; visible: page.inspected.length > 0; readOnly: true; selectByMouse: true; color: Theme.textDim; text: JSON.stringify(page.inspected, null, 2) }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("SVG mappings: [{\"path\":\"exact/scanned/resource.svg\",\"glyph\":\"home\"}]. Glyphs: home, play, settings, volume, controls, capture, fullscreen, back, apps. Icons use the chosen family and accent. Verify your target mapping; replacing an image does not add a functional sidebar button."); color: Theme.textDim }
                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox { id: iconTarget; Layout.fillWidth: true; model: page.svgResources; textRole: "path" }
                        ComboBox { id: iconGlyph; model: ["home", "play", "settings", "volume", "controls", "capture", "fullscreen", "back", "apps"] }
                        ActionButton { text: qsTr("Map icon"); enabled: page.svgResources.length > 0; onClicked: page.addMapping() }
                    }
                    TextArea { objectName: "skinIconMappings"; Layout.fillWidth: true; Layout.preferredHeight: 80; text: page.mappingJson; color: Theme.text; font.family: "Consolas"; selectByMouse: true; wrapMode: TextEdit.Wrap; onTextChanged: if (activeFocus) { page.mappingJson = text; page.plan = {}; } }
                }
            }
            Card {
                Layout.fillWidth: true
                ColumnLayout {
                    width: parent.width; spacing: 10
                    Label { text: qsTr("Review before you write"); color: Theme.text; font.pixelSize: 16; font.bold: true }
                    Flow {
                        Layout.fillWidth: true; spacing: 10; implicitHeight: childrenRect.height
                        ActionButton { text: qsTr("1. Stage custom pack"); enabled: !backend.busy; onClicked: page.stage() }
                        ActionButton { text: qsTr("2. Verify Dry Run"); enabled: !backend.busy && backend.workspaceOperations.length > 0; onClicked: { page.plan = {}; const r = page.run("dry-run"); if (r.ok) { page.planPath = backend.exePath; page.plan = r; } } }
                        ActionButton { text: qsTr("3. Review & apply…"); accent: true; enabled: !backend.busy && !!page.plan.ok && page.planPath === backend.exePath; onClicked: applyDialog.open() }
                        ActionButton { text: qsTr("Workspace →"); onClicked: page.navigate(9) }
                    }
                    Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("Staging never clears someone else's batch. Editing a preview does not change the queued batch. Close all player instances before Apply. Hash-verified snapshots are available in Backups."); color: Theme.textDim }
                    TextArea { Layout.fillWidth: true; visible: !!page.plan.ok; readOnly: true; selectByMouse: true; color: Theme.textDim; wrapMode: TextEdit.Wrap; text: page.plan.ok ? JSON.stringify(page.plan, null, 2) : "" }
                }
            }
        }
    }
    FontLoader { id: branding; source: "../fonts/NotoSansMath-Regular.ttf" }
    FileDialog { id: openProfile; nameFilters: ["Skin profiles (*.json)"]; onAccepted: { const r = page.run("skin-load", {file: backend.localFilePath(selectedFile)}); if (r.ok) page.loadProfile(r.profile); } }
    FileDialog { id: saveProfile; fileMode: FileDialog.SaveFile; defaultSuffix: "json"; nameFilters: ["Skin profiles (*.json)"]; onAccepted: { try { page.run("skin-save", {file: backend.localFilePath(selectedFile), profile: page.profile()}); } catch (e) { page.notify(String(e)); } } }
    Dialog {
        id: applyDialog; anchors.centerIn: parent; modal: true; title: qsTr("Apply verified skin batch?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { text: qsTr("Review the plan below first. Only supported palette/design values and your explicit icon mappings are written. Close all emulator instances. A recovery snapshot is retained."); wrapMode: Text.Wrap; width: 410 }
        onAccepted: { if (page.plan.ok && page.planPath === backend.exePath) { const hash = page.plan.outputSha256; page.plan = {}; page.run("apply", {outputSha256: hash}); } }
    }
}
