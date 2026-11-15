import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import BSICustomizer
import "../SkinCatalog.js" as Catalog
import "../Icons.js" as Icons

ColumnLayout {
    id: studio
    signal notify(string message)
    property var draft: initialProfile()
    property string mappings: "[]"
    property string status: qsTr("Design locally → manually Scan Resources → Generate & stage → Workspace Dry Run → Apply. Export alone does not change the emulator.")
    readonly property color accent: /^#[0-9a-fA-F]{6}$/.test(draft.accent) ? draft.accent : "#ed455d"
    readonly property var svgPaths: visible ? backend.resourceInfos.filter(row => row.path.toLowerCase().endsWith(".svg")).map(row => row.path) : []
    spacing: 12
    function initialProfile() {
        const p = Catalog.create(0);
        p.name = "𝓥𝓞𝓘𝓓 / Studio"; p.accent = "#ed455d"; p.surface = "#0b0b0e";
        p.radius = 12; p.motion = "rise"; p.iconStyle = "rounded";
        return p;
    }
    function edit(key, value) { const p = Object.assign({}, draft); p[key] = value; draft = p; }
    function profile() { const p = Object.assign({}, draft); p.icons = JSON.parse(mappings); return p; }
    function run(action, options) {
        try { const r = backend.workspace(action, options || {profile: profile()}); status = r.msg || ""; notify(status); return r; }
        catch(e) { status = String(e); notify(status); return {ok:false}; }
    }
    function stage() { run("queue-studio"); }
    function inspectCoverage() { run("skin-plan"); }
    function loadProfile(p) { draft = p; mappings = JSON.stringify(p.icons || [], null, 2); }
    function addMapping(svg) {
        try {
            if (target.currentIndex < 0) throw new Error(qsTr("Manually scan and select an exact SVG resource first."));
            const path = target.currentText;
            const rows = JSON.parse(mappings).filter(row => row.path !== path);
            const row = {path:path};
            if (svg) row.svg = svg; else row.glyph = glyph.currentText;
            rows.push(row); mappings = JSON.stringify(rows, null, 2);
        } catch(e) { status = String(e); }
    }

    Card {
        Layout.fillWidth: true
        ColumnLayout {
            anchors.fill: parent; spacing: 8
            MicroLabel { text: qsTr("EMULATOR UI STUDIO / NATIVE ASSETS") }
            Text { text: qsTr("Your player. Your signature."); color: Theme.text; font.pixelSize: 23; font.weight: Font.DemiBold }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                text: qsTr("A black foundation, outlined Settings navigation, sculpted controls and illuminated player chrome. Choose every accent, corner, icon and entrance effect. Native setting pages, Save, discard prompts and player actions stay intact.")
            }
        }
    }

    // A design preview, not an emulator process. No idle preview animation.
    Rectangle {
        Layout.fillWidth: true; implicitHeight: 270; radius: 16; color: "#070709"; border.color: "#25252d"
        Item {
            id: canvas
            anchors.fill: parent; anchors.margins: 12
            property real reveal: 1
            opacity: reveal
            transform: [
                Translate { x: studio.draft.motion === "slide" ? (1-canvas.reveal)*18 : 0; y: studio.draft.motion === "rise" ? (1-canvas.reveal)*14 : 0 },
                Scale { origin.x: canvas.width/2; origin.y: canvas.height/2; xScale: studio.draft.motion === "scale" ? 0.97+canvas.reveal*0.03 : 1; yScale: xScale }
            ]
            NumberAnimation { id: replay; target: canvas; property: "reveal"; from: 0; to: 1; duration: studio.draft.motion === "none" ? 0 : studio.draft.duration; easing.type: Easing.OutCubic }
            Rectangle {
                anchors.fill: parent; radius: studio.draft.radius; color: "#0b0b0e"; border.color: Qt.alpha(studio.accent, 0.25)
                Rectangle { x: parent.radius; y: 0; width: Math.max(0,parent.width-2*x); height: 1; color: studio.accent; opacity: 0.65 }
                Text { x: 15; y: 10; text: "𝓥𝓞𝓘𝓓"; color: "#f0f0f3"; font.pixelSize: 13; font.weight: Font.DemiBold }
                Text { anchors.right: parent.right; anchors.rightMargin: 14; y: 10; text: "−   □   ×"; color: "#8a8a96"; font.pixelSize: 12 }
                Rectangle {
                    x: 12; y: 42; width: parent.width-66; height: parent.height-56; radius: studio.draft.radius; color: "#101013"; border.color: Qt.alpha(studio.accent, 0.3)
                    Text { x: 14; y: 12; text: qsTr("Settings"); color: "#f1f1f5"; font.pixelSize: 16; font.weight: Font.DemiBold }
                    Column {
                        x: 10; y: 42; spacing: 5; width: Math.min(126, parent.width*0.35)
                        Repeater {
                            model: [qsTr("Performance"), qsTr("Display"), qsTr("Graphics"), qsTr("Preferences")]
                            Rectangle {
                                required property string modelData
                                required property int index
                                width: parent.width; height: 28; radius: Math.min(studio.draft.radius, 9)
                                color: index === 0 ? Qt.alpha(studio.accent, 0.07) : "transparent"
                                border.width: index === 0 ? 1 : 0; border.color: studio.accent
                                Text { anchors.verticalCenter: parent.verticalCenter; x: 10; text: parent.modelData; color: parent.index === 0 ? "#efeff4" : "#868691"; font.pixelSize: 10 }
                            }
                        }
                    }
                    Column {
                        x: Math.min(156, parent.width*0.4); y: 47; width: Math.max(50,parent.width-x-18); spacing: 12
                        Text { text: qsTr("Frame rate"); color: "#ccccd5"; font.pixelSize: 11 }
                        Rectangle {
                            width: parent.width; height: 4; radius: 2; color: "#303039"
                            Rectangle { width: parent.width*0.65; height: 4; radius: 2; color: studio.accent }
                            Rectangle { x: parent.width*0.65-5; y: -3; width: 10; height: 10; radius: 5; color: "#f4f4f8" }
                        }
                        Row { spacing: 8
                            Rectangle { width: 29; height: 16; radius: 8; color: studio.accent; Rectangle { x: 15; y: 3; width: 10; height: 10; radius: 5; color: "#fafafa" } }
                            Text { text: qsTr("High frame rate"); color: "#9696a4"; font.pixelSize: 10 }
                        }
                        Text { width: parent.width; text: qsTr("Design preview · native layout retained"); color: "#666674"; font.pixelSize: 9; wrapMode: Text.WordWrap }
                    }
                }
                Column {
                    anchors.right: parent.right; anchors.rightMargin: 10; y: 46; spacing: 7
                    Repeater {
                        model: [Icons.settings, Icons.dashboard, Icons.boot, Icons.fps]
                        Rectangle {
                            required property string modelData
                            width: 29; height: 32; radius: Math.min(studio.draft.radius, 10)
                            color: hover.containsMouse ? Qt.alpha(studio.accent,0.14) : "#121217"
                            border.color: hover.containsMouse ? studio.accent : "#24242c"
                            Behavior on color { ColorAnimation { duration: studio.draft.motion === "none" ? 0 : studio.draft.duration } }
                            Image { anchors.centerIn: parent; width: 17; height: 17; source: Icons.icon(parent.modelData, studio.accent, studio.draft.iconStyle) }
                            MouseArea { id: hover; anchors.fill: parent; hoverEnabled: true }
                        }
                    }
                }
            }
        }
    }
    Flow {
        Layout.fillWidth: true; spacing: 8
        ActionButton { text: qsTr("Replay motion"); onClicked: { replay.stop(); canvas.reveal = 1; if (!Theme.reduceMotion) replay.restart(); } }
        ActionButton { text: qsTr("Load design JSON"); enabled: !backend.busy; onClicked: openProfile.open() }
        ActionButton { text: qsTr("Save design JSON"); enabled: !backend.busy; onClicked: saveProfile.open() }
    }
    Card {
        Layout.fillWidth: true
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            MicroLabel { text: qsTr("01 / DESIGN TOKENS") }
            StudioTextField { Layout.fillWidth: true; placeholderText: qsTr("Design name"); text: studio.draft.name; onTextEdited: studio.edit("name", text) }
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Accent"); color: Theme.textDim; font.pixelSize: 12 }
                ColorField { Layout.fillWidth: true; value: studio.draft.accent; onEdited: hex => studio.edit("accent", hex) }
            }
            Flow {
                Layout.fillWidth: true; spacing: 10
                ColumnLayout {
                    Text { text: qsTr("Corners / px"); color: Theme.textDim; font.pixelSize: 11 }
                    StudioSpinBox { from: 0; to: 32; value: studio.draft.radius; onValueModified: studio.edit("radius", value) }
                }
                ColumnLayout {
                    Text { text: qsTr("Motion"); color: Theme.textDim; font.pixelSize: 11 }
                    StudioComboBox { model: ["none", "fade", "rise", "slide", "scale"]; currentIndex: Math.max(0, model.indexOf(studio.draft.motion)); onActivated: studio.edit("motion", currentText) }
                }
                ColumnLayout {
                    Text { text: qsTr("Duration / ms"); color: Theme.textDim; font.pixelSize: 11 }
                    StudioSpinBox { from: 0; to: 1200; stepSize: 20; value: studio.draft.duration; onValueModified: studio.edit("duration", value) }
                }
                ColumnLayout {
                    Text { text: qsTr("Icon family"); color: Theme.textDim; font.pixelSize: 11 }
                    StudioComboBox { model: ["line", "rounded", "bold"]; currentIndex: Math.max(0, model.indexOf(studio.draft.iconStyle)); onActivated: studio.edit("iconStyle", currentText) }
                }
            }
            Text { Layout.fillWidth: true; text: qsTr("Black base retained. Entrance choices animate the Settings shell; shared popup transitions fade. Sidebar edge breathing follows motion on/off. Boot animation is never replaced."); wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11 }
        }
    }
    Card {
        Layout.fillWidth: true
        ColumnLayout {
            anchors.fill: parent; spacing: 10
            MicroLabel { text: qsTr("02 / YOUR ICONS") }
            Text { Layout.fillWidth: true; text: qsTr("Built-in icon families apply automatically. For individual replacements, manually Scan Resources on the Resources page, select an exact SVG target, then choose a glyph or upload your own static SVG. Hover/click variants are separate targets. The preview above illustrates the icon family, not custom target mappings."); wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11 }
            StudioComboBox { id: target; Layout.fillWidth: true; model: studio.svgPaths }
            Flow {
                Layout.fillWidth: true; spacing: 8
                StudioComboBox { id: glyph; model: ["settings", "home", "back", "controls", "volume", "capture", "apps", "fullscreen", "close", "menu", "memory", "rotate"] }
                ActionButton { text: qsTr("Map glyph"); enabled: !backend.busy && target.currentIndex >= 0; onClicked: studio.addMapping("") }
                ActionButton { text: qsTr("Map custom SVG…"); enabled: !backend.busy && target.currentIndex >= 0; onClicked: openIcon.open() }
                ActionButton { text: qsTr("Clear mappings"); onClicked: studio.mappings = "[]" }
            }
            StudioTextArea { Layout.fillWidth: true; Layout.preferredHeight: 100; text: studio.mappings; font.family: Theme.monoFont; wrapMode: TextEdit.Wrap; onTextChanged: if (activeFocus) studio.mappings = text }
        }
    }
    Card {
        Layout.fillWidth: true
        ColumnLayout {
            anchors.fill: parent; spacing: 10
            MicroLabel { text: qsTr("03 / GENERATE + CONNECT") }
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 12
                text: qsTr("Creates Theme.qml, Icons.js, SettingsShell.qml, BSISettingsOutline.qml, NativeControlStyle.qml, SidebarSkin/Visual, BSISidebarVisual and TopbarSkin/BSITopbarVisual in Assets/BSIStudio/studio-<hash>. Verified native QML imports that bundle after Apply. Unknown/compiled-only player contracts are refused, not reported as styled. Existing files, installer media and boot stay untouched. Settings/About replacement tools can conflict: restore that shell first if coverage is refused.")
            }
            Flow {
                Layout.fillWidth: true; spacing: 8
                ActionButton { text: qsTr("Check native coverage"); enabled: !backend.busy; onClicked: studio.inspectCoverage() }
                ActionButton { text: qsTr("Export Assets bundle…"); enabled: !backend.busy; onClicked: exportFolder.open() }
                ActionButton { objectName: "generateEmulatorUi"; text: qsTr("Generate & stage native UI"); enabled: !backend.busy; onClicked: studio.stage() }
            }
            Text { objectName: "emulatorUiStatus"; Layout.fillWidth: true; text: studio.status; textFormat: Text.PlainText; wrapMode: Text.WordWrap; color: Theme.text; font.pixelSize: 12 }
            Text { Layout.fillWidth: true; text: qsTr("Review and apply in Workspace. Restore the matching EXE snapshot from Backups to undo. Bundles are retained for older recovery points—do not delete an active bundle or move the emulator without restaging. This is not a replacement optimizer, Android theme or native Windows dialog skin."); wrapMode: Text.WordWrap; color: Theme.textDim; font.pixelSize: 11 }
        }
    }
    FileDialog {
        id: openIcon; nameFilters: ["Static SVG (*.svg)"]
        onAccepted: { const r = studio.run("skin-icon-load", {file: backend.localFilePath(selectedFile)}); if (r.ok) studio.addMapping(r.svg); }
    }
    FileDialog {
        id: openProfile; nameFilters: ["Design JSON (*.json)"]
        onAccepted: { const r = studio.run("skin-load", {file: backend.localFilePath(selectedFile)}); if (r.ok) studio.loadProfile(r.profile); }
    }
    FileDialog {
        id: saveProfile; fileMode: FileDialog.SaveFile; defaultSuffix: "json"; nameFilters: ["Design JSON (*.json)"]
        onAccepted: { try { studio.run("skin-save", {file: backend.localFilePath(selectedFile), profile: studio.profile()}); } catch(e) { studio.status = String(e); } }
    }
    FolderDialog {
        id: exportFolder
        onAccepted: { try { studio.run("studio-export", {folder: backend.localFilePath(selectedFolder), profile: studio.profile()}); } catch(e) { studio.status = String(e); } }
    }
}
