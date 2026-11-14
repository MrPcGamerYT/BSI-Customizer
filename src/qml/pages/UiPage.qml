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
                           links: [], showSections: true, sectionsHidden: false,
                           compiledPage: false, strings: [] })
    // the link rows read from the page, one editable row each
    property var aboutLinks: []
    // whether the two sections the MSI build draws hidden are switched on
    property bool showSectionsWanted: true
    // the last Apply/Restore answer, kept on the card (a toast can be missed)
    property string aboutMsg: ""
    // The buttons of the moved row, in the order they are shown, with the ones
    // the user hid. Staged here and written by the card's Apply button.
    property var buttonRows: []
    property bool buttonOrderReady: false
    property bool buttonOrderDirty: false
    property string buttonOrderDetail: ""
    property int currentSection: 0
    property int aboutPreviewRevision: 0
    // Player Appearance Studio (native fallback). The HTML workspace exposes
    // the same catalog; both stage through backend.workspace so Dry Run and
    // recovery stay native and identical.
    property var appearanceRows: []
    property string appearanceMsg: qsTr("Inspect the selected player to discover safe numeric appearance tokens.")
    property var appearancePlan: ({})
    // In-player Settings window rebuild (native mirror of the Chromium
    // "Settings panel rebuild" tab). The options live on this PC; the player
    // only changes when the Apply button runs the real patcher.
    property var panel: ({ supported: false, applied: false, restorable: false, tabs: [],
                           settingsPageCount: 0, settingsQmlResourceCount: 0,
                           compiledUnitsDetected: 0, compiledUnitsActive: 0,
                           embeddedQmlResourceCount: 0, qmlResourceSamples: [],
                           logoStatus: "", logoFallback: false,
                           detail: qsTr("Load the player UI to inspect its Settings window.") })
    // hiddenTabs/tabIcons come from the saved profile, not from the player.
    property var panelProfile: ({ hiddenTabs: [], tabIcons: {}, logo: null })
    property string panelMsg: ""
    property bool panelReady: false

    readonly property var previewLinks: {
        const revision = page.aboutPreviewRevision;
        return page.collectLinks().filter(link => !link.hidden && (link.label || link.name || link.url));
    }

    // Delegate roots add properties beyond QQuickItem. Mark those lookups as
    // dynamic rather than treating a generic itemAt() result as a typed row.
    function removalItem(index: int): var { return removalList.itemAtIndex(index); }
    function linkItem(index: int): var { return linkRepeater.itemAt(index); }

    function recountChecked() {
        let n = 0;
        for (let i = 0; i < removalList.count; i++) {
            const it = page.removalItem(i);
            if (it !== null && it.checked)
                n++;
        }
        page.checkedCount = n;
    }

    function refreshAppearance() {
        const r = backend.workspace("appearance-inspect", {});
        page.appearanceMsg = r.msg || "";
        page.appearancePlan = ({});
        const rows = [];
        if (r.ok) {
            const staged = (backend.customizationProfile || {}).appearance || {};
            for (const token of (r.tokens || [])) {
                const row = token;
                row.edit = staged[token.key] !== undefined ? staged[token.key] : token.value;
                rows.push(row);
            }
        }
        page.appearanceRows = rows;
    }

    function setAppearanceValue(index, value) {
        if (index < 0 || index >= page.appearanceRows.length)
            return;
        const rows = page.appearanceRows.slice();
        rows[index].edit = value;
        page.appearanceRows = rows;
    }

    function collectAppearanceEdits(changedOnly) {
        const edits = {};
        for (const row of page.appearanceRows) {
            if (!changedOnly || row.edit !== row.value)
                edits[row.key] = row.edit;
        }
        return edits;
    }

    function stageAppearance() {
        const edits = page.collectAppearanceEdits(true);
        if (Object.keys(edits).length === 0) {
            page.appearanceMsg = qsTr("No appearance values differ from the inspected player.");
            return;
        }
        const r = backend.workspace("queue-appearance", { edits: edits });
        page.appearanceMsg = r.msg || "";
        page.appearancePlan = ({});
        page.notify(page.appearanceMsg);
    }

    function dryRunAppearance() {
        page.appearancePlan = backend.workspace("dry-run", {});
        page.appearanceMsg = page.appearancePlan.msg || "";
        page.notify(page.appearanceMsg);
    }

    function applyAppearance() {
        if (!page.appearancePlan.ok || !page.appearancePlan.outputSha256)
            return;
        const r = backend.workspace("apply", { outputSha256: page.appearancePlan.outputSha256 });
        page.appearanceMsg = r.msg || "";
        page.appearancePlan = ({});
        if (r.ok)
            page.refreshAppearance();
        page.notify(page.appearanceMsg);
    }

    function saveAppearanceProfile() {
        const r = backend.workspace("profile-section", { section: "appearance", value: page.collectAppearanceEdits(false) });
        page.appearanceMsg = r.msg || "";
        page.notify(page.appearanceMsg);
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

    property string lastProfileAbout: JSON.stringify((backend.customizationProfile || {}).about || {})
    Connections {
        target: backend
        function onCustomizationProfileChanged() {
            const a = (backend.customizationProfile || {}).about || {};
            const value = JSON.stringify(a); if (value === page.lastProfileAbout) return;
            page.lastProfileAbout = value;
            if (a.text !== undefined) aboutText.text = a.text;
            if (a.showSections !== undefined) page.showSectionsWanted = !!a.showSections;
            if (a.links !== undefined) page.aboutLinks = a.links;
        }
    }
    function refreshAbout(loadFields) {
        const state = backend.aboutPageState();
        page.about = state;
        if (loadFields) {
            page.aboutLinks = state.links || [];
            const staged = (backend.customizationProfile || {}).about || {};
            aboutText.text = staged.text !== undefined ? staged.text : (state.text || "");
            if (staged.links !== undefined) page.aboutLinks = staged.links;
            page.showSectionsWanted = staged.showSections !== undefined ? !!staged.showSections : state.showSections !== false;
        }
    }

    // Every row as it is edited right now, handed to backend.applyAboutPage().
    function collectLinks() {
        const out = [];
        for (let i = 0; i < linkRepeater.count; i++) {
            const item = page.linkItem(i);
            if (!item)
                continue;
            // The checkbox says “Show”, so a ticked row is visible: the engine
            // wants the opposite flag. Passing it straight through hid every
            // row the user kept and showed the ones they unticked.
            out.push({ id: item.linkId, name: item.linkName, url: item.linkUrl,
                       label: item.linkLabel, hidden: !item.linkHidden });
        }
        return out;
    }

    // The About card reads the player, so it follows the file: on first show,
    // whenever the path changes, and every time the tab is opened again (the
    // EXE may have been patched or restored from another tab meanwhile).
    function refreshButtonRows() {
        const state = backend.sidebarButtonRows();
        page.buttonOrderReady = !!(state.supported && state.applied);
        // Without the layout there is no row to order, so the card shows the
        // reason instead of a list left over from before the restore.
        page.buttonRows = page.buttonOrderReady ? (state.buttons || []) : [];
        page.buttonOrderDetail = state.detail || "";
        page.buttonOrderDirty = false;
    }

    // Move one row by delta (-1 up, +1 down). The order is only staged; the
    // card's Apply button writes it into the player.
    function moveButtonRow(index, delta) {
        const rows = page.buttonRows.slice();
        const to = index + delta;
        if (to < 0 || to >= rows.length)
            return;
        const moved = rows[index];
        rows[index] = rows[to];
        rows[to] = moved;
        page.buttonRows = rows;
        page.buttonOrderDirty = true;
    }

    function setButtonHidden(index, hidden) {
        const rows = page.buttonRows.slice();
        if (index < 0 || index >= rows.length)
            return;
        const row = rows[index];
        if (!row.canHide)
            return;
        rows[index] = { key: row.key, label: row.label, hidden: hidden, canHide: row.canHide };
        page.buttonRows = rows;
        page.buttonOrderDirty = true;
    }

    // reset -> empty order, which the patcher fills with its default row.
    function applyButtonOrder(reset) {
        const keys = [];
        const hidden = [];
        if (!reset) {
            for (let i = 0; i < page.buttonRows.length; i++) {
                keys.push(page.buttonRows[i].key);
                if (page.buttonRows[i].hidden)
                    hidden.push(page.buttonRows[i].key);
            }
        }
        const result = backend.applySidebarOrder(keys, hidden);
        page.refreshLayout();
        page.refreshButtonRows();
        if (result && result.msg)
            page.notify(result.msg);
    }

    // ---------------- in-player Settings window rebuild ----------------

    // Everything the rebuild card shows: what the player contains right now
    // plus the options saved on this PC (hidden tabs, custom artwork).
    function refreshPanel() {
        page.panel = backend.settingsPanelRebuildState();
        const profile = settings.settingsPanelProfile || {};
        page.panelProfile = { hiddenTabs: profile.hiddenTabs || [],
                              tabIcons: profile.tabIcons || {},
                              logo: profile.logo || null };
        page.panelReady = true;
    }

    function panelTabVisible(id) {
        const hidden = page.panelProfile.hiddenTabs || [];
        return hidden.indexOf(id) < 0;
    }

    // The tabs are a checkbox each; the tick writes the saved profile, never
    // the player. The last visible tab cannot be hidden - the window would
    // have no native page left to show.
    function setPanelTabVisible(id, visible) {
        const hidden = (page.panelProfile.hiddenTabs || []).slice();
        const at = hidden.indexOf(id);
        if (!visible) {
            if (at >= 0)
                return;
            const total = (page.panel.tabs || []).length;
            if (total > 0 && hidden.length >= total - 1) {
                page.panelMsg = qsTr("Keep at least one native Settings tab visible.");
                return;
            }
            hidden.push(id);
        } else if (at >= 0) {
            hidden.splice(at, 1);
        }
        page.savePanelOptions(hidden);
    }

    // `name`/`tagline` are omitted when only a tab tick changed, so the saved
    // identity is kept; the two fields pass what they hold.
    function savePanelOptions(hidden, name, tagline) {
        const ids = [];
        const tabs = page.panel.tabs || [];
        for (let i = 0; i < tabs.length; ++i)
            ids.push(tabs[i].id);
        const result = backend.saveSettingsPanelProfile(
                    name === undefined ? settings.aboutDeveloperName : name,
                    tagline === undefined ? settings.aboutTagline : tagline,
                    hidden || page.panelProfile.hiddenTabs || [], ids);
        page.panelMsg = (result && result.msg) ? result.msg : "";
        if (result && result.ok) {
            page.refreshPanel();
            page.notify(result.msg);
        }
    }

    function savePanelIdentity() {
        page.savePanelOptions(undefined, panelDeveloperField.text, panelTaglineField.text);
    }

    function setPanelAsset(kind, id, path) {
        const result = backend.setSettingsPanelAsset(kind, id, path);
        page.panelMsg = (result && result.msg) ? result.msg : "";
        if (result && result.ok) {
            page.refreshPanel();
            page.notify(result.msg);
        }
    }

    function clearPanelAsset(kind, id) {
        const result = backend.clearSettingsPanelAsset(kind, id);
        page.panelMsg = (result && result.msg) ? result.msg : "";
        if (result && result.ok) {
            page.refreshPanel();
            page.notify(result.msg);
        }
    }

    // Reset drops the saved tab visibility and every custom SVG/logo; the
    // player is untouched until the rebuild is applied again.
    function resetPanelOptions() {
        const icons = page.panelProfile.tabIcons || {};
        for (const id in icons)
            backend.clearSettingsPanelAsset("tab-icon", id);
        if (page.panelProfile.logo)
            backend.clearSettingsPanelAsset("logo", "");
        page.savePanelOptions([]);
        page.refreshPanel();
        page.panelMsg = qsTr("Tab visibility and custom artwork are back to the player defaults.");
    }

    // Apply / Restore run the real patcher: it verifies every embedded QML
    // resource, writes a feature-local recovery snapshot, then commits.
    function applyPanel(enabled) {
        const result = backend.applySettingsPanelRebuild(enabled);
        page.panelMsg = (result && result.msg) ? result.msg : "";
        page.refreshPanel();
        if (result && result.msg)
            page.notify(result.msg);
    }

    function panelLabel() {
        if (page.panel.applied)
            return qsTr("Custom rebuild installed");
        if (Number(page.panel.compiledUnitsActive || 0) > 0 && page.panel.restorable)
            return qsTr("Compiled copy still active");
        return qsTr("Original panel");
    }

    function panelLogoLabel() {
        if (page.panelProfile.logo && page.panelProfile.logo.data)
            return qsTr("Custom logo selected on this PC");
        if (page.panel.logoFallback)
            return qsTr("Built-in fallback available");
        return page.panel.logoStatus ? qsTr("Player logo found") : qsTr("Not yet verified");
    }

    function panelVisibleTabCount() {
        const total = (page.panel.tabs || []).length;
        const hidden = (page.panelProfile.hiddenTabs || []).length;
        return Math.max(0, total - hidden);
    }

    property bool inspectionStale: true
    function reloadAll() {
        if (backend.busy) return;
        refreshLayout();
        refreshButtonRows();
        refreshAbout(true);
        refreshPanel();
        inspectionStale = false;
    }

    // Explicit inspection only: never re-read multi-MB EXEs on page entry.

    Connections {
        target: backend
        function onExePathChanged() {
            page.inspectionStale = true;
            page.layoutTweaks = []; page.buttonRows = []; page.buttonOrderReady = false;
            page.aboutLinks = []; page.about = {supported:false,applied:false,detail:qsTr("Load the selected player UI to inspect it.")};
            page.appearanceRows = [];
            page.appearancePlan = ({});
            page.appearanceMsg = qsTr("Target changed. Inspect appearance tokens again.");
            page.panel = ({ supported: false, applied: false, restorable: false, tabs: [],
                            settingsPageCount: 0, settingsQmlResourceCount: 0,
                            compiledUnitsDetected: 0, compiledUnitsActive: 0,
                            embeddedQmlResourceCount: 0, qmlResourceSamples: [],
                            logoStatus: "", logoFallback: false,
                            detail: qsTr("Load the player UI to inspect its Settings window.") });
            page.panelReady = false;
            page.panelMsg = "";
        }
        // Worker completion marks stale, never starts another synchronous scan.
        function onBusyChanged() { if (!backend.busy) page.inspectionStale = true; }
    }

    // The rebuild reads the whole selected EXE, so it runs on the explicit
    // Load/Refresh press and when its own tab is opened - never on page entry,
    // and not again while the state on screen is still current: the EXE may
    // only have changed through another tab, which marks the inspection stale.
    Connections {
        target: page
        function onCurrentSectionChanged() {
            // 4 = Settings panel.
            if (page.currentSection === 4 && !backend.busy
                    && (!page.panelReady || page.inspectionStale))
                page.refreshPanel();
        }
    }

    ScrollView {
        id: scroller
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: content.implicitHeight
        clip: true

        Item {
            id: content
            width: scroller.availableWidth
            height: implicitHeight
            implicitHeight: body.implicitHeight + 48

            ColumnLayout {
                id: body
                x: 24
                y: 24
                width: parent.width - 48
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
                        text: page.inspectionStale ? qsTr("Load player UI") : qsTr("Refresh UI")
                        enabled: !backend.busy
                        onClicked: page.reloadAll()
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

                StudioTabBar {
                    id: uiSections
                    objectName: "playerUiSections"
                    Layout.fillWidth: true
                    currentIndex: page.currentSection
                    onCurrentIndexChanged: page.currentSection = currentIndex
                    StudioTabButton { text: qsTr("Layout & clean-up") }
                    StudioTabButton { text: qsTr("About preview & editor") }
                    StudioTabButton { text: qsTr("Remove items") }
                    StudioTabButton { text: qsTr("Appearance studio") }
                    StudioTabButton { objectName: "settingsPanelRebuildTab"; text: qsTr("Settings panel") }
                }

                // ---------------- one-click clean-up ----------------
                Card {
                    visible: page.currentSection === 0
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
                                    page.refreshButtonRows();
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
                    visible: page.currentSection === 0
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
                                StudioCheckBox {
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
                                        page.refreshButtonRows();
                                        if (result && result.msg)
                                            page.notify(result.msg);
                                    }
                                }
                            }
                        }
                    }
                }

                Card {
                    visible: page.currentSection === 0
                    Layout.fillWidth: true
                    pad: 14

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8

                        Text {
                            text: qsTr("Top-bar button order")
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.Bold
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("The row the sidebar becomes sits in the middle of the title bar, "
                                       + "exactly like before the order could be changed. Move any button "
                                       + "up or down, including Back, Home and Recent, or untick it to "
                                       + "leave it out. Only the buttons shown here exist in this player "
                                       + "and the list follows the layout automatically.")
                            color: Theme.textDim
                            font.pixelSize: 11
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            visible: !page.buttonOrderReady
                            text: page.buttonOrderDetail.length > 0
                                  ? page.buttonOrderDetail
                                  : qsTr("Apply the title-bar layout first; the order belongs to it.")
                            color: Theme.warn
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }

                        Repeater {
                            model: page.buttonRows
                            delegate: RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                required property var modelData
                                required property int index

                                Text {
                                    Layout.preferredWidth: 22
                                    text: (index + 1)
                                    color: Theme.textDim
                                    font.pixelSize: 11
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    color: modelData.hidden ? Theme.textDim : Theme.text
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                                Text {
                                    visible: !modelData.canHide
                                    text: qsTr("always shown")
                                    color: Theme.textDim
                                    font.pixelSize: 10
                                }
                                // Every row can be moved - Back, Home and Recent
                                // included; only the tick is limited to the
                                // buttons that may be left out.
                                ActionButton {
                                    implicitHeight: 24
                                    implicitWidth: 26
                                    text: "\u2191"
                                    enabled: page.buttonOrderReady && index > 0
                                    onClicked: page.moveButtonRow(index, -1)
                                }
                                ActionButton {
                                    implicitHeight: 24
                                    implicitWidth: 26
                                    text: "\u2193"
                                    enabled: page.buttonOrderReady && index < page.buttonRows.length - 1
                                    onClicked: page.moveButtonRow(index, 1)
                                }
                                StudioCheckBox {
                                    id: showCheck
                                    visible: modelData.canHide
                                    enabled: page.buttonOrderReady
                                    checked: !modelData.hidden
                                    padding: 0
                                    implicitWidth: 18
                                    implicitHeight: 18
                                    indicator: Rectangle {
                                        implicitWidth: 18
                                        implicitHeight: 18
                                        radius: 5
                                        color: showCheck.checked ? Theme.accent : "transparent"
                                        border.width: 1
                                        border.color: showCheck.checked ? Theme.accent
                                                                        : Theme.borderStrong
                                        Text {
                                            anchors.centerIn: parent
                                            visible: showCheck.checked
                                            text: "\u2713"
                                            color: Theme.primaryText
                                            font.pixelSize: 11
                                            font.weight: Font.Black
                                        }
                                    }
                                    onToggled: page.setButtonHidden(index, !checked)
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            ActionButton {
                                accent: true
                                implicitHeight: 28
                                enabled: page.buttonOrderReady && page.buttonRows.length > 0
                                         && page.buttonOrderDirty
                                text: qsTr("Apply order")
                                onClicked: page.applyButtonOrder(false)
                            }
                            ActionButton {
                                implicitHeight: 28
                                enabled: page.buttonOrderReady
                                text: qsTr("Reset to default")
                                onClicked: page.applyButtonOrder(true)
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }
                }

                Card {
                    visible: page.currentSection === 2
                    Layout.fillWidth: true
                    // A scroll view gives its content unbounded height, so the
                    // removal list gets its own fixed box and scrolls inside it
                    // instead of stretching the whole page.
                    Layout.preferredHeight: 360
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
                                        const it = page.removalItem(i);
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
                                        const it = page.removalItem(i);
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

                // ---------------- Player Appearance Studio ----------------
                Card {
                    visible: page.currentSection === 3
                    Layout.fillWidth: true
                    pad: 18

                    ColumnLayout {
                        width: parent.width
                        spacing: 10

                        Text {
                            text: qsTr("Player Appearance Studio")
                            color: Theme.text
                            font.pixelSize: 16
                            font.bold: true
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: Theme.textDim
                            text: qsTr("Safe numeric QML tokens only: font sizes, button radius/density/spacing, control indicators and player chrome. Inspect, stage, Dry Run, then Apply through the same verified Workspace transaction. This is not a raw QML/hex editor and does not install font or icon packs.")
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            ActionButton { text: qsTr("Inspect tokens"); enabled: !backend.busy; onClicked: page.refreshAppearance() }
                            ActionButton { text: qsTr("Stage edits"); enabled: page.appearanceRows.length > 0 && !backend.busy; onClicked: page.stageAppearance() }
                            ActionButton { text: qsTr("Dry Run"); enabled: backend.workspaceOperations.length > 0 && !backend.busy; onClicked: page.dryRunAppearance() }
                            ActionButton { text: qsTr("Apply verified batch"); accent: true; enabled: !!page.appearancePlan.ok && !backend.busy; onClicked: appearanceApplyDialog.open() }
                            ActionButton { text: qsTr("Save to profile"); enabled: page.appearanceRows.length > 0 && !backend.busy; onClicked: page.saveAppearanceProfile() }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: Theme.textDim
                            text: page.appearanceMsg
                        }
                        Repeater {
                            model: page.appearanceRows
                            delegate: RowLayout {
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                spacing: 10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text { text: modelData.label + " · " + modelData.group; color: Theme.text; font.pixelSize: 12 }
                                    Text { text: modelData.key + " · current " + modelData.value; color: Theme.textDim; font.pixelSize: 10; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                }
                                StudioSpinBox {
                                    from: modelData.minimum
                                    to: modelData.maximum
                                    stepSize: modelData.step || 1
                                    value: modelData.edit
                                    editable: true
                                    onValueModified: page.setAppearanceValue(index, value)
                                }
                            }
                        }
                        StudioTextArea {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 120
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            color: Theme.textDim
                            text: page.appearancePlan.msg ? JSON.stringify(page.appearancePlan, null, 2) : ""
                        }
                    }
                }

                // ---------------- Settings → About ----------------
                Card {
                    visible: page.currentSection === 1
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
                                               + "and address. Edit them, untick the ones you do not "
                                               + "want and apply — the page keeps its layout, logo and "
                                               + "version, a hidden link leaves nothing behind (not even "
                                               + "its dash) and the “Check for updates” button is "
                                               + "removed.")
                                    color: Theme.textDim
                                    font.pixelSize: 11
                                }
                            }

                            ActionButton {
                                Layout.alignment: Qt.AlignTop
                                implicitHeight: 30
                                text: qsTr("Diagnose")
                                onClicked: {
                                    const report = backend.aboutPageReport();
                                    page.aboutMsg = report;
                                    console.log(report);
                                }
                            }

                            ActionButton {
                                Layout.alignment: Qt.AlignTop
                                text: qsTr("Restore stock About")
                                implicitHeight: 30
                                enabled: !!page.about.applied
                                onClicked: {
                                    const result = backend.applyAboutPage("", [], false, false);
                                    page.aboutMsg = (result && result.msg) ? result.msg : "";
                                    page.refreshAbout(true);
                                    if (result && result.msg) {
                                        page.notify(result.msg);
                                        console.log(result.msg);
                                    }
                                }
                            }
                        }

                        // What the tool actually read out of the player, verbatim. A build
                        // whose page cannot be parsed shows its own first lines here instead
                        // of an empty card, so "it does not read" becomes a readable fact.
                        Rectangle {
                            Layout.fillWidth: true
                            visible: page.about.source !== "" && !page.about.supported
                            implicitHeight: aboutSource.implicitHeight + 16
                            radius: 8
                            color: Theme.raised
                            border.width: 1
                            border.color: Theme.warn

                            Text {
                                id: aboutSource
                                anchors.fill: parent
                                anchors.margins: 8
                                text: page.about.source
                                color: Theme.textDim
                                font.pixelSize: 10
                                font.family: "Consolas"
                                wrapMode: Text.Wrap
                                maximumLineCount: 8
                                elide: Text.ElideRight
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
                                      ? qsTr("Sections shown — switch back to stock")
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
                                        ? qsTr("This build draws “Important links” and “About and "
                                               + "other info” with visible: uiBackend.oem !== \"msi5\", "
                                               + "which is hidden on MSI installs. Applying forces both "
                                               + "sections visible, so the text and the rows below are "
                                               + "always shown.")
                                        : qsTr("The sections keep the build's own oem binding, so an MSI "
                                               + "install still hides them. Press “Unhide links + About” "
                                               + "to switch them on.");
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
                        ActionButton {
                            text: qsTr("Save About settings to profile")
                            onClicked: { const r = backend.workspace("profile-section", {section: "about", value: {text: aboutText.text, showSections: page.showSectionsWanted, links: page.collectLinks()}}); page.aboutMsg = r.msg || ""; }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 210
                            spacing: 10

                            StudioTextArea {
                                id: aboutText
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                wrapMode: TextArea.Wrap
                                selectByMouse: true
                                placeholderText: qsTr("Edit the text shown by the player's About page…")
                                color: Theme.text
                                placeholderTextColor: Theme.textDim
                                font.family: "Consolas"
                                font.pixelSize: 12
                                onTextChanged: page.aboutPreviewRevision++
                                background: Rectangle {
                                    color: Theme.raised
                                    radius: 8
                                    border.width: 1
                                    border.color: aboutText.activeFocus ? Theme.accent : Theme.border
                                }
                            }

                            Rectangle {
                                Layout.preferredWidth: 300
                                Layout.fillHeight: true
                                radius: 10
                                color: Theme.cardLow
                                border.width: 1
                                border.color: Theme.border
                                clip: true

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 8

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Rectangle {
                                            Layout.preferredWidth: 28; Layout.preferredHeight: 28
                                            radius: 8; color: Theme.accentSoft
                                            Text { anchors.centerIn: parent; text: "B"; color: Theme.accentText; font.pixelSize: 16; font.weight: Font.Bold }
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 1
                                            Text { text: qsTr("BlueStacks · About"); color: Theme.text; font.pixelSize: 11; font.weight: Font.DemiBold }
                                            Text { text: qsTr("Live preview · text only"); color: Theme.textDim; font.pixelSize: 9 }
                                        }
                                    }

                                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.hairline }

                                    Text {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        text: aboutText.text || qsTr("Your About text will appear here.")
                                        color: Theme.soft
                                        font.pixelSize: 11
                                        wrapMode: Text.Wrap
                                        verticalAlignment: Text.AlignTop
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        visible: !page.showSectionsWanted
                                        text: qsTr("About links are currently set to remain hidden.")
                                        color: Theme.warn
                                        font.pixelSize: 9
                                        wrapMode: Text.Wrap
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        visible: page.showSectionsWanted
                                        spacing: 4
                                        Repeater {
                                            model: page.previewLinks
                                            delegate: Text {
                                                required property var modelData
                                                Layout.fillWidth: true
                                                text: "•  " + (modelData.label || modelData.name || modelData.url || qsTr("Link"))
                                                color: Theme.accentText
                                                font.pixelSize: 9
                                                elide: Text.ElideRight
                                            }
                                        }
                                        Text {
                                            visible: page.previewLinks.length === 0
                                            text: qsTr("No visible links selected")
                                            color: Theme.textDim
                                            font.pixelSize: 9
                                        }
                                    }
                                }
                            }
                        }
                        // A TextEdit has no maximumLength (only a single-line
                        // TextField does), so the room is counted here and the
                        // line says so before Apply has to refuse. The sentence's
                        // record does not grow either.
                        Text {
                            Layout.fillWidth: true
                            visible: page.about.textMax > 0
                            wrapMode: Text.WordWrap
                            color: aboutText.length > page.about.textMax ? Theme.warn : Theme.textDim
                            font.pixelSize: 10
                            text: aboutText.length > page.about.textMax
                                  ? qsTr("%1 characters — the page's record holds %2, so Apply "
                                         + "will refuse this. Shorten it by %3.")
                                        .arg(aboutText.length)
                                        .arg(page.about.textMax)
                                        .arg(aboutText.length - page.about.textMax)
                                  : qsTr("%1 of %2 characters — this is what the page's record holds")
                                        .arg(aboutText.length)
                                        .arg(page.about.textMax)
                        }

                        // ---- the stock links ----------------------------------
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                text: qsTr("Links — the words before the link, the text it prints "
                                           + "and the address it opens; untick “Show” to remove one")
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

                        // Column headings: the displayed text and the address
                        // are two different fields, and it must be obvious.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: page.aboutLinks.length > 0
                            Text {
                                text: qsTr("Show")
                                Layout.preferredWidth: 62
                                color: Theme.textDim
                                font.pixelSize: 10
                            }
                            Text {
                                text: qsTr("Name")
                                Layout.preferredWidth: 190
                                color: Theme.textDim
                                font.pixelSize: 10
                            }
                            Text {
                                text: qsTr("Text shown in the row")
                                Layout.preferredWidth: 190
                                color: Theme.textDim
                                font.pixelSize: 10
                            }
                            Text {
                                text: qsTr("Address it opens")
                                Layout.fillWidth: true
                                color: Theme.textDim
                                font.pixelSize: 10
                            }
                        }

                        Repeater {
                            id: linkRepeater
                            model: page.aboutLinks

                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 2

                                // Read back by collectLinks() when Apply is pressed.
                                property string linkId: modelData.id
                                property alias linkName: linkNameField.text
                                // What the anchor prints is its own field: the
                                // address it opens is edited next to it.
                                property alias linkLabel: linkLabelField.text
                                property alias linkUrl: linkUrlField.text
                                property alias linkHidden: linkShow.checked

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    StudioCheckBox {
                                        id: linkShow
                                        text: qsTr("Show")
                                        checked: !modelData.hidden
                                        font.pixelSize: 11
                                        onToggled: page.aboutPreviewRevision++
                                        palette.windowText: Theme.text
                                        palette.buttonText: Theme.text
                                        property bool checkedToggle: checked
                                    }
                                    StudioTextField {
                                        id: linkNameField
                                        Layout.preferredWidth: 190
                                        // The page's record for this row cannot
                                        // grow, so the field stops where the room
                                        // does instead of failing at Apply.
                                        maximumLength: modelData.nameMax > 0 ? modelData.nameMax
                                                                             : 32767
                                        text: modelData.name
                                        onTextEdited: page.aboutPreviewRevision++
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
                                    StudioTextField {
                                        id: linkLabelField
                                        Layout.preferredWidth: 190
                                        maximumLength: modelData.labelMax > 0 ? modelData.labelMax
                                                                              : 32767
                                        text: modelData.label !== undefined ? modelData.label : ""
                                        onTextEdited: page.aboutPreviewRevision++
                                        placeholderText: qsTr("text it prints")
                                        color: Theme.text
                                        placeholderTextColor: Theme.textDim
                                        font.pixelSize: 12
                                        background: Rectangle {
                                            color: Theme.raised
                                            radius: 8
                                            border.width: 1
                                            border.color: linkLabelField.activeFocus ? Theme.accent : Theme.border
                                        }
                                    }
                                    StudioTextField {
                                        id: linkUrlField
                                        Layout.fillWidth: true
                                        maximumLength: modelData.urlMax > 0 ? modelData.urlMax : 32767
                                        text: modelData.url
                                        onTextEdited: page.aboutPreviewRevision++
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

                                // A compiled record cannot grow, so each field
                                // stops where its record does. Saying the number
                                // out loud is the difference between "the field
                                // is broken" and "this row has 7 characters".
                                Text {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 70
                                    wrapMode: Text.WordWrap
                                    color: Theme.textDim
                                    font.pixelSize: 10
                                    text: modelData.nameMax > 0
                                          ? qsTr("Fits: %1 characters for the name, %2 for the "
                                                 + "text it prints, %3 for the address")
                                                .arg(modelData.nameMax)
                                                .arg(modelData.labelMax)
                                                .arg(modelData.urlMax)
                                          : qsTr("Fits: a .qml row is a text file - no length limit "
                                                 + "here")
                                }
                            }
                        }

                        // What the window prints. A BlueStacks player ships the
                        // page twice: the .qml this card edits and a compiled
                        // copy, and the compiled one is what is on screen. Its
                        // words are listed here so the editor cannot claim a
                        // page it never reached - and so a build that answers
                        // "nothing changed" shows exactly why.
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            visible: !!page.about.compiledPage
                                     && (page.about.strings || []).length > 0

                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: qsTr("Text the window prints — read from the compiled page and "
                                           + "rewritten in place when you apply, never grown")
                                color: Theme.textDim
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }
                            Repeater {
                                model: (page.about.strings || []).slice(0, 40)
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text {
                                        text: (modelData.index + 1) + "."
                                        color: Theme.textDim
                                        font.pixelSize: 10
                                        Layout.preferredWidth: 24
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.text
                                        elide: Text.ElideRight
                                        color: Theme.text
                                        font.pixelSize: 11
                                        font.family: "Consolas"
                                    }
                                    Text {
                                        text: qsTr("%1 ch").arg(modelData.max)
                                        color: Theme.textDim
                                        font.pixelSize: 10
                                        horizontalAlignment: Text.AlignRight
                                        Layout.preferredWidth: 46
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
                                    page.aboutMsg = (result && result.msg) ? result.msg : "";
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
                                                + "prints now; the update button will be removed.")
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
                            visible: page.aboutMsg.length > 0
                            text: page.aboutMsg
                            font.family: "Consolas"
                            color: page.about.applied ? Theme.green : Theme.warn
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
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

                // ---------------- in-player Settings window rebuild --------
                // Native mirror of the Chromium "Settings panel rebuild" tab: it
                // centres the native page content, keeps any subset of the live
                // Settings tabs, swaps tab/About artwork and rewrites the About
                // identity - all through the same verified patcher and its
                // feature-local recovery snapshot.
                Card {
                    visible: page.currentSection === 4
                    Layout.fillWidth: true
                    pad: 18

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 12

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Text {
                                    text: qsTr("Rebuild the BlueStacks Settings panel")
                                    color: Theme.text
                                    font.pixelSize: 15
                                    font.weight: Font.Bold
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: qsTr("Customize the in-player Settings window: centre the "
                                               + "native page content, choose which native tabs stay "
                                               + "visible, give each tab its own SVG icon and edit the "
                                               + "About logo, name and links. These choices are saved "
                                               + "on this PC; the selected HD-Player.exe changes only "
                                               + "when you apply.")
                                    color: Theme.textDim
                                    font.pixelSize: 11
                                }
                            }

                            ActionButton {
                                Layout.alignment: Qt.AlignTop
                                implicitHeight: 30
                                text: qsTr("Re-inspect")
                                enabled: !backend.busy
                                onClicked: page.refreshPanel()
                            }
                        }

                        // ---- what the player holds right now --------------
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                MicroLabel { Layout.preferredWidth: 200; font.pixelSize: 9; text: qsTr("Patch support") }
                                Text {
                                    Layout.fillWidth: true
                                    text: page.panel.supported ? qsTr("Supported") : qsTr("Not supported")
                                    color: page.panel.supported ? Theme.text : Theme.warn
                                    font.pixelSize: 12
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                MicroLabel { Layout.preferredWidth: 200; font.pixelSize: 9; text: qsTr("Current panel") }
                                Text {
                                    Layout.fillWidth: true
                                    text: page.panelLabel()
                                    color: page.panel.applied ? Theme.green : Theme.text
                                    font.pixelSize: 12
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                MicroLabel { Layout.preferredWidth: 200; font.pixelSize: 9; text: qsTr("Native Settings tabs") }
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("%1 discovered · %2 visible")
                                          .arg(Number(page.panel.settingsPageCount || 0))
                                          .arg(page.panelVisibleTabCount())
                                    color: Theme.text
                                    font.pixelSize: 12
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                MicroLabel { Layout.preferredWidth: 200; font.pixelSize: 9; text: qsTr("Verified QML dependencies") }
                                Text {
                                    Layout.fillWidth: true
                                    text: String(Number(page.panel.settingsQmlResourceCount || 0))
                                    color: Theme.text
                                    font.pixelSize: 12
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                MicroLabel { Layout.preferredWidth: 200; font.pixelSize: 9; text: qsTr("Embedded compiled QML") }
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("%1 found · %2 active")
                                          .arg(Number(page.panel.compiledUnitsDetected || 0))
                                          .arg(Number(page.panel.compiledUnitsActive || 0))
                                    color: Number(page.panel.compiledUnitsActive || 0) > 0
                                           ? Theme.warn : Theme.text
                                    font.pixelSize: 12
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                MicroLabel { Layout.preferredWidth: 200; font.pixelSize: 9; text: qsTr("Logo verification") }
                                Text {
                                    Layout.fillWidth: true
                                    text: page.panelLogoLabel()
                                    color: Theme.text
                                    font.pixelSize: 12
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: page.panel.detail
                                   || qsTr("Inspect the selected player UI to check its embedded QML "
                                           + "resources.")
                            color: page.panel.supported ? Theme.textDim : Theme.warn
                            font.pixelSize: 11
                        }

                        // The registered QML resources, verbatim - the same list the
                        // Chromium tab shows behind its details disclosure.
                        Rectangle {
                            Layout.fillWidth: true
                            visible: (page.panel.qmlResourceSamples || []).length > 0
                            implicitHeight: panelSamples.implicitHeight + 16
                            radius: 8
                            color: Theme.raised
                            border.width: 1
                            border.color: Theme.border

                            Text {
                                id: panelSamples
                                anchors.fill: parent
                                anchors.margins: 8
                                text: qsTr("Verified QML resources (")
                                      + Number(page.panel.embeddedQmlResourceCount || 0)
                                      + qsTr(" registered)\n") + (page.panel.qmlResourceSamples || []).join("\n")
                                color: Theme.textDim
                                font.family: "Consolas"
                                font.pixelSize: 10
                                wrapMode: Text.Wrap
                                maximumLineCount: 8
                                elide: Text.ElideRight
                            }
                        }

                        // ---- native tab visibility + per-tab artwork --------
                        Text {
                            text: qsTr("Native Settings tabs")
                            color: Theme.text
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("Untick a tab to hide it — at least one native tab always stays "
                                       + "visible. Custom SVGs are embedded into the rebuilt window; "
                                       + "only self-contained SVG files are accepted.")
                            color: Theme.textDim
                            font.pixelSize: 10
                            font.italic: true
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            visible: (page.panel.tabs || []).length === 0
                            text: qsTr("Inspect a supported player to list its live tabs.")
                            color: Theme.textDim
                            font.pixelSize: 11
                        }

                        Repeater {
                            model: page.panel.tabs || []
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 8

                                StudioCheckBox {
                                    id: panelTabCheck
                                    Layout.alignment: Qt.AlignVCenter
                                    checked: page.panelTabVisible(modelData.id)
                                    padding: 0
                                    implicitWidth: 18
                                    implicitHeight: 18
                                    indicator: Rectangle {
                                        implicitWidth: 18
                                        implicitHeight: 18
                                        radius: 5
                                        color: panelTabCheck.checked ? Theme.accent : "transparent"
                                        border.width: 1
                                        border.color: panelTabCheck.checked ? Theme.accent
                                                                            : Theme.borderStrong
                                        Text {
                                            anchors.centerIn: parent
                                            visible: panelTabCheck.checked
                                            text: "\u2713"
                                            color: Theme.primaryText
                                            font.pixelSize: 11
                                            font.weight: Font.Black
                                        }
                                    }
                                    onToggled: page.setPanelTabVisible(modelData.id, checked)
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.label || modelData.page || modelData.id
                                        color: page.panelTabVisible(modelData.id) ? Theme.text
                                                                                  : Theme.textDim
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.page || modelData.id
                                        color: Theme.textDim
                                        font.pixelSize: 10
                                        elide: Text.ElideMiddle
                                    }
                                }

                                MicroLabel {
                                    Layout.alignment: Qt.AlignVCenter
                                    visible: !!(page.panelProfile.tabIcons || {})[modelData.id]
                                    text: qsTr("custom SVG")
                                    color: Theme.accentText
                                }
                                ActionButton {
                                    implicitHeight: 26
                                    text: (page.panelProfile.tabIcons || {})[modelData.id]
                                          ? qsTr("Replace SVG") : qsTr("Choose SVG")
                                    onClicked: {
                                        panelAssetDialog.targetKind = "tab-icon";
                                        panelAssetDialog.targetId = modelData.id;
                                        panelAssetDialog.nameFilters = [qsTr("Self-contained SVG (*.svg)")];
                                        panelAssetDialog.open();
                                    }
                                }
                                ActionButton {
                                    implicitHeight: 26
                                    visible: !!(page.panelProfile.tabIcons || {})[modelData.id]
                                    text: qsTr("Clear")
                                    onClicked: page.clearPanelAsset("tab-icon", modelData.id)
                                }
                            }
                        }

                        // ---- About identity -------------------------------
                        Text {
                            text: qsTr("About page identity")
                            color: Theme.text
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("This is the About page inside the emulator’s rebuilt Settings "
                                       + "window. The logo and the fields below come from your local "
                                       + "profile; the links are edited in the About preview & editor "
                                       + "tab and applied with them.")
                            color: Theme.textDim
                            font.pixelSize: 10
                            font.italic: true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Rectangle {
                                implicitWidth: 56
                                implicitHeight: 56
                                radius: 12
                                color: Theme.cardLow
                                border.width: 1
                                border.color: Theme.border

                                Image {
                                    id: panelLogoPreview
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    fillMode: Image.PreserveAspectFit
                                    visible: status === Image.Ready
                                    source: {
                                        const logo = page.panelProfile.logo;
                                        if (logo && logo.data)
                                            return "data:" + (logo.mime || "image/svg+xml") + ";base64," + logo.data;
                                        return "";
                                    }
                                }
                                Text {
                                    anchors.centerIn: parent
                                    visible: !panelLogoPreview.visible
                                    text: qsTr("Logo")
                                    color: Theme.textDim
                                    font.pixelSize: 10
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Text {
                                    text: qsTr("About logo")
                                    color: Theme.text
                                    font.pixelSize: 12
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: (page.panelProfile.logo && page.panelProfile.logo.data)
                                          ? qsTr("A validated custom logo is selected in your local "
                                                 + "profile; apply the rebuild to write it to the player.")
                                          : (page.panel.logoStatus
                                             || qsTr("Choose a custom logo or use the safe built-in fallback."))
                                    color: Theme.textDim
                                    font.pixelSize: 10
                                }
                            }

                            ActionButton {
                                Layout.alignment: Qt.AlignTop
                                implicitHeight: 28
                                text: (page.panelProfile.logo && page.panelProfile.logo.data)
                                      ? qsTr("Replace logo…") : qsTr("Choose logo…")
                                onClicked: {
                                    panelAssetDialog.targetKind = "logo";
                                    panelAssetDialog.targetId = "";
                                    panelAssetDialog.nameFilters = [qsTr("Logo image (*.svg *.png *.jpg *.jpeg *.webp)")];
                                    panelAssetDialog.open();
                                }
                            }
                            ActionButton {
                                Layout.alignment: Qt.AlignTop
                                implicitHeight: 28
                                visible: !!(page.panelProfile.logo && page.panelProfile.logo.data)
                                text: qsTr("Use player/fallback logo")
                                onClicked: page.clearPanelAsset("logo", "")
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Text {
                                    text: qsTr("Name under logo")
                                    color: Theme.textDim
                                    font.pixelSize: 11
                                }
                                StudioTextField {
                                    id: panelDeveloperField
                                    Layout.fillWidth: true
                                    text: settings.aboutDeveloperName
                                    placeholderText: qsTr("Mr.Pc Gamer & Romulos")
                                    onEditingFinished: page.savePanelIdentity()
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Text {
                                    text: qsTr("Subtitle")
                                    color: Theme.textDim
                                    font.pixelSize: 11
                                }
                                StudioTextField {
                                    id: panelTaglineField
                                    Layout.fillWidth: true
                                    text: settings.aboutTagline
                                    placeholderText: qsTr("BlueStacks tools · with contributor Nova")
                                    onEditingFinished: page.savePanelIdentity()
                                }
                            }
                        }

                        // ---- actions -------------------------------------
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            ActionButton {
                                implicitHeight: 30
                                text: qsTr("Save rebuild options")
                                enabled: !backend.busy
                                onClicked: page.savePanelIdentity()
                            }
                            ActionButton {
                                implicitHeight: 30
                                text: qsTr("Reset tab visibility & artwork")
                                enabled: !backend.busy
                                onClicked: page.resetPanelOptions()
                            }
                            Item { Layout.fillWidth: true }
                            ActionButton {
                                implicitHeight: 30
                                text: qsTr("Restore original Settings panel")
                                enabled: !backend.busy && !!page.panel.restorable
                                onClicked: page.applyPanel(false)
                            }
                            ActionButton {
                                implicitHeight: 30
                                accent: true
                                enabled: !backend.busy
                                text: page.panel.applied ? qsTr("Update rebuilt Settings panel")
                                                         : qsTr("Apply Settings panel rebuild")
                                onClicked: page.applyPanel(true)
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            visible: page.panelMsg.length > 0
                            text: page.panelMsg
                            color: page.panel.applied ? Theme.green : Theme.textDim
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }

                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("A feature-local recovery snapshot is saved before the single EXE "
                                       + "commit, so restoring this panel never reverts other edits. "
                                       + "Close every HD-Player window first; restart BlueStacks after "
                                       + "applying or restoring.")
                            color: Theme.textDim
                            font.pixelSize: 10
                            font.italic: true
                        }
                    }
                }

            }
        }
    }
    FileDialog {
        id: panelAssetDialog
        property string targetKind: ""
        property string targetId: ""
        title: targetKind === "logo" ? qsTr("Choose the About logo")
                                     : qsTr("Choose the tab SVG icon")
        onAccepted: page.setPanelAsset(targetKind, targetId, backend.localFilePath(selectedFile))
    }
    Dialog {
        id: appearanceApplyDialog
        anchors.centerIn: parent
        modal: true
        width: Math.min(520, page.width - 32)
        title: qsTr("Apply the verified appearance batch?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label {
            wrapMode: Text.Wrap
            text: qsTr("Close all HD-Player instances. The current fingerprint and prepared output must still match Dry Run. A recovery snapshot is saved before the single EXE commit.\n\nOutput SHA-256: ") + (page.appearancePlan.outputSha256 || "")
        }
        onAccepted: page.applyAppearance()
    }
}
