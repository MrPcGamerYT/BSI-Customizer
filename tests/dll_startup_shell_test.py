"""Click the DLL patcher through the full production Main.qml navigation.

Real Qt Quick rendering/input, explicit mock auth/backend services. No production
sign-in bypass, real file access, DLL execution, license request or player launch.
"""

import os
from pathlib import Path
import shutil
import tempfile

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QSG_RHI_BACKEND", "software")
from PySide6.QtCore import QObject, QPointF, QRectF, Qt, QUrl
from PySide6.QtGui import QFont, QFontDatabase, QGuiApplication
from PySide6.QtQml import QQmlComponent, QQmlEngine
from PySide6.QtQuick import QQuickWindow
from PySide6.QtTest import QTest

app = QGuiApplication([])
source = Path(__file__).resolve().parents[1] / "src/qml"
for font in ("Inter.ttf", "JetBrainsMono.ttf"):
    assert QFontDatabase.addApplicationFont(str(source / "fonts" / font)) >= 0
app.setFont(QFont("Inter"))
screenshots = os.environ.get("BSI_UI_SCREENSHOT_DIR")
if screenshots:
    screenshots = Path(screenshots)
    screenshots.mkdir(parents=True, exist_ok=True)

with tempfile.TemporaryDirectory() as directory:
    module = Path(directory) / "BSICustomizer"
    shutil.copytree(source, module)
    entries = ["module BSICustomizer", "singleton Theme 1.0 Theme.qml"]
    entries += [
        f"{p.stem} 1.0 {p.relative_to(module).as_posix()}"
        for p in module.rglob("*.qml")
        if p.name != "Theme.qml"
    ]
    (module / "qmldir").write_text("\n".join(entries))
    engine = QQmlEngine()
    engine.addImportPath(directory)
    warnings = []
    engine.warnings.connect(lambda es: warnings.extend(e.toString() for e in es))
    components, objects = [], []

    def create(text):
        component = QQmlComponent(engine)
        components.append(component)
        component.setData(text.encode(), QUrl.fromLocalFile(str(Path(directory) / "fixture.qml")))
        obj = component.create()
        assert obj is not None, [e.toString() for e in component.errors()]
        QQmlEngine.setObjectOwnership(obj, QQmlEngine.CppOwnership)
        objects.append(obj)
        return obj

    backend = create(
        """import QtQuick
QtObject {
 property bool busy: false
 property string exePath: "C:/UI-test/HD-Player.exe"
 property var installs: []
 property var resourceInfos: []
 property var workspaceOperations: []
 property var calls: []
 property bool installed: false
 signal log(string message, string color)
 signal workspaceChanged()
 function workspace(action, options) { calls=calls.concat(["workspace:"+action]); return {ok:false}; }
 function dllStartupInspect(name, symbol) {
  calls=calls.concat(["inspect:"+name+"!"+symbol]); workspaceChanged();
  return {ok:true,canApply:!installed,canRestore:installed,alreadyApplied:installed,
          exeSha256:"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
          dllSha256:"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
          warning:"UI fixture only. No EXE or DLL is accessed by this mock service.",
          dllDirectImports:["KERNEL32.dll"],msg:"UI fixture inspection; no files accessed"};
 }
 function dllStartupApply(name, exe, dll, ack, symbol) {
  if(name!=="zlag.dll" || symbol!=="zlag_init" || !ack || exe.length!==64 || dll.length!==64)
   throw Error("Unreviewed UI fixture apply");
  calls=calls.concat(["apply:"+name+"!"+symbol]); installed=true; workspaceChanged();
  return {ok:true,changed:true,msg:"UI fixture apply; no files accessed"};
 }
 function dllStartupRestore() { throw Error("This navigation test must not restore files"); }
}"""
    )
    themes = create(
        """import QtQuick
QtObject { property bool reduceMotion:true; property bool backgroundMotion:false;
 property int theme:0; property int effectiveTheme:0; property color accentColor:"#7c5cff" }"""
    )
    auth = create(
        """import QtQuick
QtObject {
 property bool authorized:false
 property bool busy:false
 property bool hasSavedSession:false
 property var savedUser:({})
 property string savedMeta:""
 property real progress:0
 property bool progressIndeterminate:false
 property string note:""
 property string noteKind:""
 property string inviteUrl:""
 property string setupHint:""
 function signOut() { authorized=false; }
 function startDiscord() { return {Manual:true}; }
 function continueSaved() {}
 function completeManual(value) {}
 function forgetSaved() {}
}"""
    )
    updates = create(
        """import QtQuick
QtObject {
 property bool available:false
 property bool skipped:false
 property string state:"idle"
 property string noteKind:""
 property string latestVersion:""
 property string downloadHost:""
 property string downloadSize:""
 property string releaseNotes:""
 property real progress:0
 property bool readyToInstall:false
}"""
    )
    for name, obj in (("backend", backend), ("themes", themes), ("auth", auth), ("updates", updates)):
        engine.rootContext().setContextProperty(name, obj)
    window = create("import BSICustomizer\nMain {}")
    assert isinstance(window, QQuickWindow)
    window.setMinimumWidth(640)
    window.setMinimumHeight(400)
    window.resize(1440, 930)

    def wait_for(predicate, message):
        for _ in range(250):
            if predicate():
                return
            QTest.qWait(20)
        raise AssertionError(message)

    def visual_items():
        pending = [window.contentItem()]
        while pending:
            item = pending.pop()
            yield item
            pending.extend(item.childItems())

    def named(name):
        matches = [item for item in visual_items() if item.objectName() == name]
        if matches:
            assert len(matches) == 1, f"Duplicate visible editor/control: {name}"
            return matches[0]
        obj = window.findChild(QObject, name)
        assert obj is not None, name
        return obj

    def reveal(item):
        # Scroll real Flickables into view; never invoke a hidden button's signal.
        ancestors = []
        parent = item.parentItem()
        while parent:
            ancestors.append(parent)
            parent = parent.parentItem()
        for parent in reversed(ancestors):
            if parent.metaObject().indexOfProperty("contentY") < 0:
                continue
            top = item.mapToItem(parent, QPointF(0, 0)).y()
            bottom = top + item.height()
            delta = top - 8 if top < 8 else max(0, bottom - parent.height() + 8)
            height = parent.property("contentHeight")
            if isinstance(height, (int, float)):
                maximum = max(0, height - parent.height())
                parent.setProperty("contentY", max(0, min(maximum, parent.property("contentY") + delta)))
                QTest.qWait(30)
        point = item.mapToScene(QPointF(item.width() / 2, item.height() / 2))
        visible = QRectF(0, 0, window.width(), window.height())
        for parent in [item, *ancestors]:
            assert parent.isVisible() and parent.opacity() > 0, f"Hidden control: {item.objectName()}"
            if parent.clip():
                visible = visible.intersected(parent.mapRectToScene(parent.boundingRect()))
        assert item.width() > 0 and item.height() > 0 and visible.contains(point), (
            item.objectName(),
            point,
            visible,
        )
        return point.toPoint()

    def click(name):
        item = named(name)
        assert item.isEnabled(), f"Disabled control: {name}"
        point = reveal(item)
        QTest.mouseClick(window, Qt.LeftButton, Qt.NoModifier, point)
        QTest.qWait(70)

    def calls():
        value = backend.property("calls")
        return value.toVariant() if hasattr(value, "toVariant") else value

    collection = named("featureAddons")
    loader = named("featureDllStartup")
    QTest.qWait(100)
    assert collection.property("item") is None and loader.property("item") is None
    assert calls() == [], "locked shell must not inspect files"
    auth.setProperty("authorized", True)  # The test-local mock only, never production auth.
    wait_for(lambda: collection.property("item") is not None, "Collection did not load")
    assert loader.property("item") is None and calls() == []

    # Actual pointer clicks from the main sidebar, not navigatePage()/clicked().
    click("navGroup1")
    nav = named("featureNav13")
    assert nav.property("contentItem").property("text") == "DLL patcher"
    click("featureNav13")
    wait_for(lambda: loader.property("item") is not None, "PATCHER > DLL patcher did not load")
    page = loader.property("item")
    assert window.property("currentPage") == 13
    assert named("dllStartupPageTitle").property("text") == "DLL patcher"
    assert named("startupDllName").property("text") == "zlag.dll"
    assert named("startupDllExport").property("text") == "zlag_init"
    assert calls() == [], "navigating to the actual page must not inspect/apply"
    if screenshots:
        QTest.qWait(100)
        assert window.grabWindow().save(str(screenshots / "dll-patcher-main-window.png"))

    # Collection's above-the-fold shortcut opens the SAME retained editor.
    for width, height in ((1440, 930), (980, 640), (691, 475)):
        window.resize(width, height)
        QTest.qWait(100)
        before = calls()
        click("navGroup6")
        assert window.property("currentPage") == 12
        click("openStartupDllPatcher")
        assert window.property("currentPage") == 13 and loader.property("item") == page
        assert calls() == before, "navigation/resize must not run the file engine"
        assert len([i for i in visual_items() if i.objectName() == "dllStartupPanel"]) == 1
        click("inspectStartupDll")
        assert calls() == before + ["inspect:zlag.dll!zlag_init"]
        click("applyStartupDll")
        assert named("startupDllConfirm").property("opened")
        assert not named("confirmStartupDllApply").isEnabled(), "acknowledgement defaults OFF"
        QTest.keyClick(window, Qt.Key_Escape)
        QTest.qWait(50)
        assert not named("startupDllConfirm").property("opened")
        assert not any(value.startswith("apply:") for value in calls()), "cancel must not apply"
        print(f"PASS: full shell pointer navigation, one editor, inspect + cancel at {width}x{height}")

    # Exercise confirmation through real input too, exclusively against mock data.
    click("applyStartupDll")
    click("startupDllAcknowledge")
    assert named("confirmStartupDllApply").isEnabled()
    click("confirmStartupDllApply")
    assert calls()[-2:] == ["apply:zlag.dll!zlag_init", "inspect:zlag.dll!zlag_init"]
    assert named("restoreStartupDll").isEnabled()
    backend.setProperty("busy", True)
    assert not named("inspectStartupDll").isEnabled() and not named("restoreStartupDll").isEnabled()
    backend.setProperty("busy", False)
    auth.setProperty("authorized", False)
    QTest.qWait(100)
    assert loader.property("item") is None and collection.property("item") is None
    assert not warnings, warnings
    window.close()
    objects.clear()
    engine.deleteLater()
    QTest.qWait(20)
print(
    "PASS: full Main.qml DLL patcher navigation, physical input, responsive scrolling, consent, busy and auth gates (mock services)"
)
