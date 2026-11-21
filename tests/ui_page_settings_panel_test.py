"""Load the real native Player-interface page in Qt 6.8 with a mock backend.

Checks that the Settings-panel rebuild section exists as a fifth tab next to
its siblings, renders the inspected player state, and drives profile save,
apply and restore with the native tab list. Physical full-Main.qml navigation
also exercises cleanup migration and the one combined title-details choice.

Requires PySide6==6.8.3. No player executable is touched.
QT_QPA_PLATFORM=offscreen python3 tests/ui_page_settings_panel_test.py
"""
import os
import base64
import shutil
import tempfile
import sys
import traceback
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QSG_RHI_BACKEND", "software")
from PySide6.QtCore import QObject, QUrl, QCoreApplication, QEvent, QMetaObject, QPoint, QPointF, QRectF, Qt
from PySide6.QtGui import QGuiApplication, QWindow
from PySide6.QtQml import QQmlComponent, QQmlEngine
from PySide6.QtQuick import QQuickItem
from PySide6.QtTest import QTest

if os.environ.get("GITHUB_ACTIONS"):
    def report_failure(kind, value, trace):
        for line in "".join(traceback.format_exception(kind, value, trace)).splitlines():
            escaped = line.replace("%", "%25").replace("\r", "%0D")
            print(f"::error file=tests/ui_page_settings_panel_test.py::{escaped}", flush=True)
        sys.__excepthook__(kind, value, trace)
    sys.excepthook = report_failure

app = QGuiApplication([])
source = Path(__file__).resolve().parents[1] / "src/qml"
pack = source.parent / "assets/settings-panel-icons"
def svg_asset(name):
    svg = (pack / (name + ".svg")).read_text().replace('<svg ', '<svg color="#e7e9f2" ', 1)
    return {"mime": "image/svg+xml", "data": base64.b64encode(svg.encode()).decode()}
DEFAULT_ICONS = {"graphics": svg_asset("graphics"), "display": svg_asset("display")}
CUSTOM_ICON = {"mime": "image/svg+xml", "data": base64.b64encode(
    b'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><circle cx="12" cy="12" r="9" fill="#55aaff"/></svg>').decode()}

PANEL_STATE = {
    "supported": True,
    "applied": False,
    "restorable": False,
    "settingsPageCount": 7,
    "settingsQmlResourceCount": 12,
    "compiledUnitsDetected": 2,
    "compiledUnitsActive": 0,
    "embeddedQmlResourceCount": 34,
    "qmlResourceSamples": ["UiSettingsWindow.qml", "UiThemeObject.qml"],
    "tabs": [
        {"id": "graphics", "page": "SettingsGraphics.qml", "label": "Graphics", "index": 0},
        {"id": "display", "page": "SettingsDisplay.qml", "label": "Display", "index": 1},
    ],
    "logoFallback": True,
    "logoStatus": "",
    "detail": "Editable player QML sources found.",
}

with tempfile.TemporaryDirectory() as directory:
    module = Path(directory) / "BSICustomizer"
    shutil.copytree(source, module)
    entries = ["module BSICustomizer", "singleton Theme 1.0 Theme.qml"]
    for path in module.rglob("*.qml"):
        if path.name != "Theme.qml":
            entries.append(f"{path.stem} 1.0 {path.relative_to(module).as_posix()}")
    (module / "qmldir").write_text("\n".join(entries))
    engine = QQmlEngine()
    engine.addImportPath(directory)
    errors = []
    engine.warnings.connect(lambda warnings: errors.extend(w.toString() for w in warnings))

    components = []
    instances = []

    def create(qml):
        component = QQmlComponent(engine)
        component.setParent(engine)
        components.append(component)
        component.setData(qml.encode(), QUrl.fromLocalFile(str(Path(directory) / "test.qml")))
        obj = component.create()
        assert obj is not None, [e.toString() for e in component.errors()]
        QQmlEngine.setObjectOwnership(obj, QQmlEngine.ObjectOwnership.CppOwnership)
        if not isinstance(obj, QWindow):
            obj.setParent(engine)
        instances.append(obj)
        return obj

    backend = create('''import QtQuick
QtObject {
    property string exePath: "player.exe"
    property bool busy: false
    property var installs: []
    signal workspaceChanged()
    signal resourcesChanged()
    property var resourceInfos: []
    property var themeEntries: []
    property var workspaceOperations: []
    property var customizationProfile: ({})
    property var calls: []
    property var removalNames: ["Remove title details", "Sidebar Rotate"]
    property bool cleanupNeedsUpdate: true
    property bool cleanupApplied: true
    property var hidden: []
    property var packIcons: ({})
    property var selectedAsset: ({})
    signal log(string message, string color)
    function settingsPanelRebuildState() {
        calls = calls.concat(["settingsPanelRebuildState"]);
        return {
            supported: true, applied: false, restorable: false,
            settingsPageCount: 7, settingsQmlResourceCount: 12,
            compiledUnitsDetected: 2, compiledUnitsActive: 0,
            embeddedQmlResourceCount: 34,
            qmlResourceSamples: ["UiSettingsWindow.qml", "UiThemeObject.qml"],
            tabs: [
                {id: "graphics", page: "SettingsGraphics.qml", label: "Graphics", index: 0},
                {id: "display", page: "SettingsDisplay.qml", label: "Display", index: 1}
            ],
            defaultTabIcons: packIcons,
            logoFallback: true, logoStatus: "", detail: "Editable player QML sources found."
        };
    }
    function saveSettingsPanelProfile(developerName, tagline, hiddenTabs, availableTabs, links) {
        hidden = hiddenTabs;
        const profile = settings.settingsPanelProfile || {};
        const icons = Object.assign({}, profile.linkIcons || {});
        for (const id in icons) if (!links.some(row => row.id === id)) delete icons[id];
        settings.settingsPanelProfile = Object.assign({}, profile, {hiddenTabs: hiddenTabs, links: links, linkIcons: icons});
        calls = calls.concat(["saveSettingsPanelProfile:" + developerName + "|" + tagline
                              + "|" + hiddenTabs.join(",") + "|" + availableTabs.join(",")]);
        return {ok: true, msg: "Options saved."};
    }
    function setSettingsPanelAsset(kind, id, path) {
        calls = calls.concat(["setSettingsPanelAsset:" + kind + ":" + id + ":" + path]);
        const profile = settings.settingsPanelProfile || {};
        const key = kind === "link-icon" ? "linkIcons" : "tabIcons";
        const icons = Object.assign({}, profile[key] || {});
        icons[id] = selectedAsset;
        const next = Object.assign({}, profile); next[key] = icons;
        settings.settingsPanelProfile = next;
        return {ok: true, msg: "Asset saved."};
    }
    function clearSettingsPanelAsset(kind, id) {
        calls = calls.concat(["clearSettingsPanelAsset:" + kind + ":" + id]);
        const profile = settings.settingsPanelProfile || {};
        const key = kind === "link-icon" ? "linkIcons" : "tabIcons";
        const icons = Object.assign({}, profile[key] || {});
        delete icons[id];
        const next = Object.assign({}, profile); next[key] = icons;
        settings.settingsPanelProfile = next;
        return {ok: true, msg: "Asset cleared."};
    }
    // Mirrors the real patcher's answers: normally ok, but a player patched by
    // an older run with a damaged snapshot is refused until its backup returns.
    property bool recoveryBlocked: false
    function applySettingsPanelRebuild(enabled) {
        calls = calls.concat(["applySettingsPanelRebuild:" + enabled]);
        if (recoveryBlocked)
            return {ok: false, msg: "This player carries a Settings-panel patch, but its "
                    + "recovery snapshot is unusable (incomplete). The executable was not "
                    + "overwritten; restore the player backup before rebuilding."};
        return {ok: true, msg: enabled ? "Applied." : "Restored."};
    }
    function restoreBackup(path) {
        calls = calls.concat(["restoreBackup:" + path]);
        recoveryBlocked = false;
        return {ok: true, msg: "Restored from backup."};
    }
    function layoutTweaks() { return [{id: "clean-player-ui", supported: true,
        applied: cleanupApplied, needsUpdate: cleanupNeedsUpdate, label: "Clean player UI", detail: ""}]; }
    function applyLayoutTweak(id, enabled) {
        calls = calls.concat(["applyLayoutTweak:" + id + ":" + enabled]);
        cleanupApplied = enabled; cleanupNeedsUpdate = false;
        return {ok: true, msg: "Clean UI updated."};
    }
    function applyRemovals(names) {
        calls = calls.concat(["applyRemovals:" + names.join(",")]);
        return {ok: true, msg: "Removed title details only."};
    }
    function sidebarButtonRows() { return {supported: false, applied: false, buttons: [], detail: ""}; }
    function aboutPageState() { return {supported: false, applied: false, detail: "", text: "", links: []}; }
    property bool shadersInstalled: false
    property bool shadersPending: false
    property bool shadersIncompatible: false
    function shadersInspect() {
        calls = calls.concat(["shadersInspect"]);
        return {ok: !shadersPending, installed: shadersInstalled, sourcePresent: shadersInstalled,
                canApply: !shadersPending && !shadersIncompatible, canRemove: shadersInstalled && !shadersPending,
                canRecover: shadersPending, recoveryRequired: shadersPending, bundleId: "qt-5.15.2",
                exeSha256: "reviewed-shader-exe", packageSha256: "reviewed-shader-package",
                stateSha256: "reviewed-shader-state", applyReviewSha256: "reviewed-shader-runtime",
                journalSha256: shadersPending ? "reviewed-shader-journal" : "",
                msg: shadersPending ? "Pending shader transaction" : shadersIncompatible ? "Exact Qt mismatch" : "Shader fixture inspection"};
    }
    function shadersApply(id, exe, packageHash, review, ack) {
        if (id !== "qt-5.15.2" || exe !== "reviewed-shader-exe" || packageHash !== "reviewed-shader-package"
            || review !== "reviewed-shader-runtime" || ack !== true) throw Error("Unreviewed shader apply");
        calls = calls.concat(["shadersApply"]); shadersInstalled = true; workspaceChanged();
        return {ok: true, changed: true, msg: "Shader fixture installed; no files accessed"};
    }
    function shadersRemove(exe, state, ack) {
        if (exe !== "reviewed-shader-exe" || state !== "reviewed-shader-state" || ack !== true) throw Error("Unreviewed shader removal");
        calls = calls.concat(["shadersRemove"]); shadersInstalled = false; resourcesChanged();
        return {ok: true, changed: true, msg: "Shader fixture removed; no files accessed"};
    }
    function shadersRecover(journal, ack) {
        if (journal !== "reviewed-shader-journal" || ack !== true) throw Error("Unreviewed shader recovery");
        calls = calls.concat(["shadersRecover"]); shadersPending = false; workspaceChanged();
        return {ok: true, changed: true, msg: "Shader fixture recovered; no files accessed"};
    }
    function localFilePath(url) { return String(url); }
}''')
    themes = create('''import QtQuick
QtObject { property bool reduceMotion: false; property bool backgroundMotion: true;
           property int effectiveTheme: 0; property color accentColor: "#7c5cff" }''')
    settings = create('''import QtQuick
QtObject {
    property string aboutDeveloperName: "Panel Test"
    property string aboutTagline: "Native rebuild"
    property var aboutLinks: []
    property var settingsPanelProfile: ({})
    function openExeFolder() {}
}''')
    engine.rootContext().setContextProperty("backend", backend)
    engine.rootContext().setContextProperty("themes", themes)
    engine.rootContext().setContextProperty("settings", settings)
    backend.setProperty("packIcons", DEFAULT_ICONS)
    backend.setProperty("selectedAsset", CUSTOM_ICON)

    window = create('''import QtQuick
import QtQuick.Window
import BSICustomizer
Window {
    visible: true; width: 1200; height: 800
    UiPage { id: page; objectName: "uiPage"; anchors.fill: parent }
    function openPanel() { page.currentSection = 4; }
    function calls() { return backend.calls; }
}''')
    page = window.findChild(QObject, "uiPage")
    QTest.qWait(150)

    def items(root, name):
        found = []
        pending = [root]
        while pending:
            item = pending.pop()
            if item.objectName() == name:
                found.append(item)
            pending.extend(item.childItems())
        return found

    # The Settings-panel rebuild is a fifth tab in the same Player-interface
    # strip as Layout / About / Remove / Appearance.
    tab = window.findChild(QObject, "settingsPanelRebuildTab")
    assert tab is not None, "the Settings-panel rebuild tab must exist"
    strip = window.findChild(QObject, "playerUiSections")
    assert strip is not None and strip.property("count") == 6, strip.property("count")
    labels = []
    pending = [strip]
    while pending:
        item = pending.pop()
        text = item.property("text")
        if isinstance(text, str) and text:
            labels.append(text)
        pending.extend(item.childItems())
    # Each StudioTabButton exposes its label twice (control + contentItem), and
    # the walk is depth-first from the last child: de-duplicate, then reverse.
    labels = list(dict.fromkeys(labels))[::-1]
    assert labels == ["Layout & clean-up", "About preview & editor", "Remove items",
                      "Appearance studio", "Settings panel", "Shaders"], labels

    # Opening the tab is the only thing that inspects the player.
    assert "settingsPanelRebuildState" not in backend.property("calls").toVariant()

    # Before anything is inspected the section must read as "nothing loaded yet",
    # never as a player that reports a row of zeros everywhere.
    assert not page.property("panelReady"), "nothing is inspected yet"
    _empty = page.findChild(QObject, "settingsPanelEmptyHint")
    assert _empty is not None, "the not-yet-loaded hint must exist"
    assert "No player has been read yet" in (_empty.property("text") or ""), \
        _empty.property("text")
    # The live readouts and the tab list stay hidden until a player is read, so
    # the section never shows a player-shaped row of zeros with no player.
    for gated in ("settingsPanelReadouts", "settingsPanelTabList"):
        gated_block = page.findChild(QObject, gated)
        assert gated_block is not None, f"{gated} must exist"
        assert not gated_block.property("visible"), \
            f"{gated} must stay hidden before inspection"

    # The user reaches this section by clicking the tab, so click the tab itself
    # rather than assigning currentSection: an unreachable or zero-size tab
    # would pass a property assignment but never reachable with a mouse, which is
    # exactly how this section once became invisible.
    QTest.qWait(150)
    assert tab.property("width") > 8 and tab.property("height") > 8, (
        "the Settings-panel tab must have a real clickable size")
    target = tab.mapToScene(QPointF(tab.property("width") / 2, tab.property("height") / 2))
    assert 0 <= target.x() < 1200 and 0 <= target.y() < 800, (
        f"the Settings-panel tab is outside the window: {target.x()},{target.y()}")
    QTest.mouseClick(window, Qt.LeftButton, Qt.NoModifier,
                     QPoint(int(target.x()), int(target.y())))
    QTest.qWait(200)
    assert page.property("currentSection") == 4, (
        f"clicking the Settings-panel tab must open it, got section "
        f"{page.property('currentSection')}")
    assert "settingsPanelRebuildState" in backend.property("calls").toVariant()
    panel = page.property("panel").toVariant()
    assert panel["supported"] and panel["settingsPageCount"] == 7
    assert [t["id"] for t in panel["tabs"]] == ["graphics", "display"]

    # An empty profile previews the real supplied pack. Selecting a custom SVG
    # changes that tab only; Use default removes the override instead of making
    # the tab iconless or writing the defaults into QSettings.
    def icon_source(tab_id):
        # refreshPanel replaces the Repeater model, so reacquire its delegate.
        return items(window.contentItem(), "settingsPanelTabIcon-" + tab_id)[0].property("source").toString()

    assert DEFAULT_ICONS["graphics"]["data"] in icon_source("graphics")
    assert not settings.property("settingsPanelProfile").toVariant().get("tabIcons")
    from PySide6.QtCore import Q_ARG
    QMetaObject.invokeMethod(page, "setPanelAsset", Q_ARG("QVariant", "tab-icon"),
                             Q_ARG("QVariant", "graphics"), Q_ARG("QVariant", "/chosen/custom.svg"))
    QTest.qWait(50)
    assert CUSTOM_ICON["data"] in icon_source("graphics")
    assert DEFAULT_ICONS["display"]["data"] in icon_source("display")
    QMetaObject.invokeMethod(page, "clearPanelAsset", Q_ARG("QVariant", "tab-icon"),
                             Q_ARG("QVariant", "graphics"))
    QTest.qWait(50)
    assert DEFAULT_ICONS["graphics"]["data"] in icon_source("graphics")
    assert settings.property("settingsPanelProfile").toVariant()["tabIcons"] == {}
    for tab_id in DEFAULT_ICONS:
        QMetaObject.invokeMethod(page, "setPanelAsset", Q_ARG("QVariant", "tab-icon"),
                                 Q_ARG("QVariant", tab_id), Q_ARG("QVariant", "/chosen/custom.svg"))
    QMetaObject.invokeMethod(page, "resetPanelOptions")
    QTest.qWait(50)
    assert settings.property("settingsPanelProfile").toVariant()["tabIcons"] == {}
    for tab_id, asset in DEFAULT_ICONS.items():
        assert asset["data"] in icon_source(tab_id), "reset must restore every pack preview"
    calls = backend.property("calls").toVariant()
    assert "setSettingsPanelAsset:tab-icon:graphics:/chosen/custom.svg" in calls
    assert "clearSettingsPanelAsset:tab-icon:graphics" in calls
    assert not any(call.startswith("applySettingsPanelRebuild:") for call in calls)

    # Hidden-tab selection goes through the saved profile and refuses to hide
    # every native tab.
    QMetaObject.invokeMethod(page, "setPanelTabVisible",
                             __import__("PySide6.QtCore", fromlist=["Q_ARG"]).Q_ARG("QVariant", "graphics"),
                             __import__("PySide6.QtCore", fromlist=["Q_ARG"]).Q_ARG("QVariant", False))
    QTest.qWait(50)
    calls = backend.property("calls").toVariant()
    assert "saveSettingsPanelProfile:Panel Test|Native rebuild|graphics|graphics,display" in calls, calls
    hidden = backend.property("hidden").toVariant()
    assert hidden == ["graphics"], hidden

    # The last visible tab cannot be hidden (a one-tab player refuses it).
    single = dict(panel)
    single["tabs"] = [{"id": "graphics", "page": "SettingsGraphics.qml", "label": "Graphics", "index": 0}]
    page.setProperty("panel", single)
    page.setProperty("panelProfile", {"hiddenTabs": [], "tabIcons": {}, "logo": None})
    QMetaObject.invokeMethod(page, "setPanelTabVisible",
                             __import__("PySide6.QtCore", fromlist=["Q_ARG"]).Q_ARG("QVariant", "graphics"),
                             __import__("PySide6.QtCore", fromlist=["Q_ARG"]).Q_ARG("QVariant", False))
    QTest.qWait(50)
    assert "Keep at least one native Settings tab visible." == page.property("panelMsg")
    assert backend.property("hidden").toVariant() == ["graphics"]

    # Apply and Restore run the real patcher with the right flag.
    QMetaObject.invokeMethod(page, "applyPanel",
                             __import__("PySide6.QtCore", fromlist=["Q_ARG"]).Q_ARG("QVariant", True))
    QMetaObject.invokeMethod(page, "applyPanel",
                             __import__("PySide6.QtCore", fromlist=["Q_ARG"]).Q_ARG("QVariant", False))
    QTest.qWait(50)
    calls = backend.property("calls").toVariant()
    assert "applySettingsPanelRebuild:true" in calls and "applySettingsPanelRebuild:false" in calls, calls

    # A blocked rebuild must explain itself and offer the only way out: the
    # stock backup. In the normal case the hint stays out of the way entirely.
    hint = page.findChild(QObject, "settingsPanelBackupHint")
    assert hint is not None, "the backup hint must exist"
    assert not hint.property("visible"), "the backup hint must stay hidden when apply works"
    backend.setProperty("recoveryBlocked", True)
    QMetaObject.invokeMethod(page, "applyPanel",
                             __import__("PySide6.QtCore", fromlist=["Q_ARG"]).Q_ARG("QVariant", True))
    QTest.qWait(100)
    assert page.property("panelNeedsBackup"), "a blocked apply must surface the backup path"
    assert hint.property("visible"), "the backup hint must show when the snapshot is unusable"
    backend.setProperty("recoveryBlocked", False)
    QMetaObject.invokeMethod(page, "applyPanel",
                             __import__("PySide6.QtCore", fromlist=["Q_ARG"]).Q_ARG("QVariant", True))
    QTest.qWait(100)
    assert not page.property("panelNeedsBackup") and not hint.property("visible"), \
        "a successful apply must clear the backup hint"

    # The rendered card carries the readouts and the actions of the section.
    def texts(root):
        out = []
        pending = [root]
        while pending:
            item = pending.pop()
            text = item.property("text")
            if isinstance(text, str) and text:
                out.append(text)
            pending.extend(item.childItems())
        return out

    QTest.qWait(100)
    content = texts(window.contentItem())
    joined = "\n".join(content)
    for expected in ["Rebuild the BlueStacks Settings panel", "Patch support", "Supported",
                     "Custom rebuild installed" if False else "Original panel",
                     "Native Settings tabs", "7 discovered · 1 visible",
                     "Verified QML dependencies", "Embedded compiled QML", "2 found · 0 active",
                     "Logo verification", "Built-in fallback available",
                     "Editable player QML sources found.", "UiSettingsWindow.qml",
                     "Graphics", "SettingsGraphics.qml", "Apply Settings panel rebuild",
                     "Restore original Settings panel", "Save rebuild options",
                     "Reset tab visibility & artwork", "About page identity",
                     "Name under logo", "Subtitle"]:
        assert expected in joined, expected

    # Drive the actual native cleanup migration and the one combined removal
    # with physical clicks, not a source-only contract or an invoked handler.
    def click_item(item):
        assert item.isVisible() and item.isEnabled() and item.width() > 0 and item.height() > 0
        ancestors = []
        parent = item.parentItem()
        while parent:
            ancestors.append(parent)
            parent = parent.parentItem()
        for parent in reversed(ancestors):
            if parent.metaObject().indexOfProperty("contentY") < 0:
                continue
            top = item.mapToItem(parent, QPointF(0, 0)).y()
            delta = top - 8 if top < 8 else max(0, top + item.height() - parent.height() + 8)
            maximum = max(0, parent.property("contentHeight") - parent.height())
            parent.setProperty("contentY", max(0, min(maximum, parent.property("contentY") + delta)))
            QTest.qWait(30)
        point = item.mapToScene(QPointF(item.width() / 2, item.height() / 2))
        visible = QRectF(0, 0, window.width(), window.height())
        for parent in ancestors:
            assert parent.isVisible() and parent.opacity() > 0
            if parent.clip():
                visible = visible.intersected(parent.mapRectToScene(parent.boundingRect()))
        assert visible.contains(point), (item, point, visible)
        QTest.mouseClick(window, Qt.LeftButton, Qt.NoModifier, point.toPoint())
        QTest.qWait(80)

    def click_text(value):
        pending = [window.contentItem()]
        while pending:
            item = pending.pop()
            if item.isVisible() and item.property("text") == value:
                click_item(item)
                return
            pending.extend(item.childItems())
        raise AssertionError("Native UI text not reachable: " + value)

    # Physically add/edit/save more than four links using the real native
    # delegates; choosing an icon must not erase another row's typed text.
    settings.setProperty("aboutLinks", [{"kind": "github", "title": "Customizer only", "url": "https://example.org/own"}])
    link_ids = []
    for index in range(6):
        click_item(items(window.contentItem(), "settingsPanelLinkAdd")[0])
        rows = page.property("panelLinks").toVariant()
        link_id = rows[-1]["id"]
        link_ids.append(link_id)
        for name, text in (("Title", "custom link " + str(index + 1)), ("Url", "https://example.net/custom/" + str(index + 1))):
            field = items(window.contentItem(), "settingsPanelLink" + name + "-" + link_id)[0]
            click_item(field)
            for character in text:
                modifier = Qt.ShiftModifier if character.isupper() else Qt.NoModifier
                QTest.keyClick(window, Qt.Key(ord(character.upper())), modifier)
        QTest.qWait(20)
    assert len(set(link_ids)) == 6
    QMetaObject.invokeMethod(page, "setPanelAsset", Q_ARG("QVariant", "link-icon"),
                             Q_ARG("QVariant", link_ids[2]), Q_ARG("QVariant", "/chosen/link.svg"))
    click_item(items(window.contentItem(), "settingsPanelLinkRemove-" + link_ids[0])[0])
    click_text("Save rebuild options")
    saved_profile = settings.property("settingsPanelProfile").toVariant()
    assert len(saved_profile["links"]) == 5, saved_profile
    assert saved_profile["links"][1]["id"] == link_ids[2]
    assert saved_profile["links"][1]["title"] == "custom link 3", saved_profile["links"]
    assert saved_profile["linkIcons"][link_ids[2]]["data"] == CUSTOM_ICON["data"]
    own_links = settings.property("aboutLinks")
    if hasattr(own_links, "toVariant"): own_links = own_links.toVariant()
    assert own_links[0]["title"] == "Customizer only"
    for link_id in link_ids[1:]:
        click_item(items(window.contentItem(), "settingsPanelLinkRemove-" + link_id)[0])
    click_text("Save rebuild options")
    assert settings.property("settingsPanelProfile").toVariant()["links"] == []
    page.setProperty("panelLinksReady", False)
    QMetaObject.invokeMethod(page, "refreshPanel")
    assert page.property("panelLinks").toVariant() == [], "zero saved rows must not repopulate from own-About defaults"

    click_text("Layout & clean-up")
    assert page.property("currentSection") == 0
    click_text("Refresh UI" if not page.property("inspectionStale") else "Load player UI")
    clean_action = page.findChild(QObject, "cleanPlayerUiAction")
    assert clean_action.property("text") == "Update clean-up"
    assert any("Title bar: three-line menu, help" in text for text in texts(window.contentItem()))
    assert not any("Settings/menu stays" in text for text in texts(window.contentItem()))
    click_item(clean_action)
    assert backend.property("cleanupApplied") and not backend.property("cleanupNeedsUpdate")
    assert "applyLayoutTweak:clean-player-ui:true" in backend.property("calls").toVariant()
    assert clean_action.property("text") == "Restore stock UI"
    click_item(clean_action)
    assert not backend.property("cleanupApplied")
    assert "applyLayoutTweak:clean-player-ui:false" in backend.property("calls").toVariant()
    click_text("Remove items")
    assert page.property("currentSection") == 2
    removal = page.findChild(QObject, "playerUiRemovalList")
    assert removal.property("count") == 2
    assert "Titlebar Version" not in texts(window.contentItem())
    assert "Titlebar Instance Label" not in texts(window.contentItem())
    click_text("Remove title details")
    assert page.property("checkedCount") == 1
    click_item(page.findChild(QObject, "applyPlayerUiRemovals"))
    assert "applyRemovals:Remove title details" in backend.property("calls").toVariant()

    # Follow the actual default application's sidebar too. This catches a
    # perfectly working standalone page that is missing/unreachable in Main.
    window.close()
    auth = create('''import QtQuick
QtObject {
    property bool authorized: false
    property bool busy: false
    property bool hasSavedSession: false
    property var savedUser: ({})
    property string savedMeta: ""
    property real progress: 0
    property bool progressIndeterminate: false
    property string note: ""
    property string noteKind: ""
    property string inviteUrl: ""
    property string setupHint: ""
    function signOut() { authorized = false; }
    function startDiscord() { return {Manual: true}; }
    function continueSaved() {}
    function completeManual(value) {}
    function forgetSaved() {}
}''')
    updates = create('''import QtQuick
QtObject {
    property bool available: false
    property bool skipped: false
    property string state: "idle"
    property string noteKind: ""
    property string latestVersion: ""
    property string downloadHost: ""
    property string downloadSize: ""
    property string releaseNotes: ""
    property real progress: 0
    property bool readyToInstall: false
}''')
    engine.rootContext().setContextProperty("auth", auth)
    engine.rootContext().setContextProperty("updates", updates)
    themes.setProperty("reduceMotion", True)
    window = create("import BSICustomizer\nMain {}")
    window.resize(1440, 930)
    loader = window.findChild(QObject, "featureInterface")
    assert loader is not None and loader.property("item") is None
    auth.setProperty("authorized", True)  # Only the mock, never production sign-in.
    QTest.qWait(150)
    click_item(items(window.contentItem(), "navGroup1")[0])
    click_item(items(window.contentItem(), "featureNav6")[0])
    for _ in range(150):
        if loader.property("item") is not None:
            break
        QTest.qWait(20)
    page = loader.property("item")
    assert page is not None and window.property("currentPage") == 6
    for width, height in ((1440, 930), (980, 640)):
        window.resize(width, height)
        QTest.qWait(80)
        click_text("Layout & clean-up")
        backend.setProperty("cleanupApplied", True)
        backend.setProperty("cleanupNeedsUpdate", True)
        click_text("Load player UI" if page.property("inspectionStale") else "Refresh UI")
        clean_action = page.findChild(QObject, "cleanPlayerUiAction")
        assert clean_action.property("text") == "Update clean-up"
        assert any("Title bar: three-line menu, help" in text for text in texts(window.contentItem()))
        click_item(clean_action)
        assert backend.property("cleanupApplied") and not backend.property("cleanupNeedsUpdate")
        assert clean_action.property("text") == "Restore stock UI"
        click_item(clean_action)
        assert not backend.property("cleanupApplied")
        click_text("Remove items")
        assert page.findChild(QObject, "playerUiRemovalList").property("count") == 2
        if page.property("checkedCount"):
            click_text("Remove title details")
        click_text("Remove title details")
        assert page.property("checkedCount") == 1
        before_removals = backend.property("calls").toVariant().count("applyRemovals:Remove title details")
        click_item(page.findChild(QObject, "applyPlayerUiRemovals"))
        assert backend.property("calls").toVariant().count("applyRemovals:Remove title details") == before_removals + 1
        click_item(page.findChild(QObject, "settingsPanelRebuildTab"))
        assert page.property("currentSection") == 4 and page.property("panelReady")
        print(f"PASS: full Main.qml PATCHER > Player interface physical navigation, cleanup Update/Restore, combined removal and Settings tab at {width}x{height}")

        def shader_named(name):
            matches = items(window.contentItem(), name)
            obj = matches[0] if matches else window.findChild(QObject, name)
            if obj is None:
                obj = page.findChild(QObject, name)
            assert obj is not None, name
            return obj

        before_shader = backend.property("calls").toVariant()
        click_item(shader_named("nativeShadersTab"))
        assert page.property("currentSection") == 5
        assert backend.property("calls").toVariant() == before_shader, "opening Shaders never inspects or installs automatically"
        assert not shader_named("installShaders").isEnabled() and not shader_named("removeShaders").isEnabled()
        click_item(shader_named("inspectShaders"))
        assert shader_named("installShaders").isEnabled()
        click_item(shader_named("installShaders"))
        assert shader_named("shaderInstallConfirm").property("opened")
        assert not shader_named("confirmShaderOperation").isEnabled(), "acknowledgment defaults OFF"
        QMetaObject.invokeMethod(shader_named("shaderInstallPanel"), "runReviewed")
        assert backend.property("calls").toVariant().count("shadersApply") == before_shader.count("shadersApply")
        QTest.keyClick(window, Qt.Key_Escape)
        QTest.qWait(80)
        assert not shader_named("shaderInstallConfirm").property("opened"), "cancel must close without a write"
        click_item(shader_named("installShaders"))
        click_item(shader_named("shaderInstallAcknowledge"))
        assert shader_named("confirmShaderOperation").isEnabled()
        click_item(shader_named("confirmShaderOperation"))
        assert backend.property("shadersInstalled") and shader_named("removeShaders").isEnabled()
        assert "Shader fixture installed" in shader_named("shaderInstallReport").property("text")
        click_item(shader_named("removeShaders"))
        assert not shader_named("confirmShaderOperation").isEnabled(), "each operation needs a fresh acknowledgment"
        click_item(shader_named("shaderInstallAcknowledge"))
        click_item(shader_named("confirmShaderOperation"))
        assert not backend.property("shadersInstalled") and not shader_named("removeShaders").isEnabled()
        backend.setProperty("shadersPending", True)
        click_item(shader_named("inspectShaders"))
        assert shader_named("recoverShaders").isEnabled() and not shader_named("installShaders").isEnabled()
        click_item(shader_named("recoverShaders"))
        assert not shader_named("confirmShaderOperation").isEnabled()
        click_item(shader_named("shaderInstallAcknowledge"))
        click_item(shader_named("confirmShaderOperation"))
        assert not backend.property("shadersPending") and "shadersRecover" in backend.property("calls").toVariant()
        backend.setProperty("shadersIncompatible", True)
        click_item(shader_named("inspectShaders"))
        assert not shader_named("installShaders").isEnabled()
        assert "Exact Qt mismatch" in shader_named("shaderInstallReport").property("text")
        backend.setProperty("shadersIncompatible", False)
        click_item(shader_named("inspectShaders"))
        backend.setProperty("busy", True)
        QTest.qWait(30)
        assert not shader_named("inspectShaders").isEnabled() and not shader_named("installShaders").isEnabled()
        backend.setProperty("busy", False)
        click_item(shader_named("installShaders"))
        QMetaObject.invokeMethod(backend, "resourcesChanged")
        QTest.qWait(50)
        assert not shader_named("installShaders").isEnabled() and not shader_named("shaderInstallConfirm").property("opened"), "another patch invalidates the review and open confirmation"
        click_item(shader_named("inspectShaders"))
        backend.setProperty("exePath", "other-player.exe")
        QTest.qWait(30)
        assert not shader_named("installShaders").isEnabled(), "target changes invalidate shader reviews"
        backend.setProperty("exePath", "player.exe")
        click_item(shader_named("shaderPreparePanel"))
        assert page.property("currentSection") == 4, "the prerequisite link reaches the actual native panel-preparation section"
        print(f"PASS: full Main.qml native Shaders installer physical tab/inspect/cancel/ack/install/remove/recovery, exact-version refusal and stale/busy guards at {width}x{height} (mock services, no player files)")
    auth.setProperty("authorized", False)
    QTest.qWait(80)
    assert loader.property("item") is None, "sign-out must unload the feature page"

    # Known mock-only messages come from the missing About "source" text and
    # a Controls Dialog wrapped in a plain Window. The removalNames list and
    # cleanup state are real fixture contracts now, not tolerated warnings.
    fixture_noise = ("page.about.source", 'Binding loop detected for property "implicitHeight"')
    unexpected = [w for w in errors
                  if not any(token in w for token in fixture_noise + ("TypeError: Type error",
                                                                     "Unable to assign [undefined]"))]
    assert not unexpected, unexpected

    window.close()
    page = strip = tab = None
    for instance in reversed(instances):
        instance.deleteLater()
    instance = None
    instances.clear()
    window = backend = themes = settings = None
    QCoreApplication.sendPostedEvents(None, QEvent.DeferredDelete)
    for component in reversed(components):
        component.deleteLater()
    component = None
    components.clear()
    QCoreApplication.sendPostedEvents(None, QEvent.DeferredDelete)
    engine.collectGarbage()
    engine.clearComponentCache()
    engine.deleteLater()
    engine = None
    QCoreApplication.sendPostedEvents(None, QEvent.DeferredDelete)
    QTest.qWait(1)
    print("PASS: real native variable About add/type/remove/custom-icon/save/empty-reload controls; Player-interface page; Settings-panel rebuild tab renders player "
          "state, previews pack/default overrides and drives profile save/apply/restore, cleanup Settings-access migration and one combined title-details removal")
