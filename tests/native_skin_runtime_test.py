"""Real Qt Quick Skin Studio/login creation with an explicit mock backend.
This does not execute a player or certify native patching. PySide6 6.8.3 required.
"""
import os
import shutil
import tempfile
from pathlib import Path
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QSG_RHI_BACKEND", "software")
from PySide6.QtCore import QObject, QUrl, QMetaObject, Q_ARG
from PySide6.QtGui import QGuiApplication, QWindow, QFontDatabase, QFont
from PySide6.QtQuick import QQuickWindow, QQuickItem
from PySide6.QtQml import QQmlEngine, QQmlComponent
from PySide6.QtTest import QTest
app=QGuiApplication([])
source=Path(__file__).resolve().parents[1]/"src/qml"
for name in ("Inter.ttf", "JetBrainsMono.ttf"):
    assert QFontDatabase.addApplicationFont(str(source/"fonts"/name)) >= 0
app.setFont(QFont("Inter"))
screenshots=Path(os.environ["BSI_UI_SCREENSHOT_DIR"]) if os.environ.get("BSI_UI_SCREENSHOT_DIR") else None
if screenshots: screenshots.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory() as temp:
    module=Path(temp)/"BSICustomizer";shutil.copytree(source,module)
    lines=["module BSICustomizer","singleton Theme 1.0 Theme.qml"]
    lines += [f"{p.stem} 1.0 {p.relative_to(module).as_posix()}" for p in module.rglob("*.qml") if p.name!="Theme.qml"]
    (module/"qmldir").write_text("\n".join(lines))
    engine=QQmlEngine();engine.addImportPath(temp);errors=[];engine.warnings.connect(lambda es: errors.extend(e.toString() for e in es))
    components=[];objects=[]
    def create(text):
        component=QQmlComponent(engine);component.setParent(engine);components.append(component)
        component.setData(text.encode(),QUrl.fromLocalFile(str(Path(temp)/"host.qml")))
        obj=component.create();assert obj is not None,[e.toString() for e in component.errors()]
        QQmlEngine.setObjectOwnership(obj,QQmlEngine.CppOwnership)
        if not isinstance(obj,QWindow):obj.setParent(engine)
        objects.append(obj);return obj
    def calls():
        value=backend.property("calls")
        return value.toVariant() if hasattr(value,"toVariant") else value
    backend=create('''import QtQuick
QtObject {
 property bool busy: false
 property var installs: []
 signal log(string message, string color)
 property string exePath: "fixture.exe"
 property var resourceInfos: []
 property var themeEntries: []
 property var workspaceOperations: []
 property var calls: []
 signal workspaceChanged()
 function workspace(action, options) {
  calls = calls.concat([action]);
  if(action === "queue-skin") { workspaceOperations = [{kind:"theme"}]; workspaceChanged(); }
  return {ok:true,msg:"Mock result",tokens:[],outputSha256:"mock-plan"};
 }
 function loadTheme(mode) {}
 function localFilePath(url) { return String(url); }
}''')
    themes=create('''import QtQuick
QtObject { property bool reduceMotion: false; property bool backgroundMotion: true; property int theme: 0; property int effectiveTheme: 0; property color accentColor: "#7c5cff" }''')
    auth=create('''import QtQuick
QtObject {
 property bool busy: false
 property bool authorized: false
 function signOut() { authorized=false; }
 property bool hasSavedSession: true
 property var savedUser: ({DisplayName:"Test identity"})
 property string savedMeta: "Fixture session"
 property real progress: 0
 property bool progressIndeterminate: false
 property string note: ""
 property string noteKind: ""
 property string inviteUrl: ""
 property string setupHint: ""
 function startDiscord() { return {Manual:true}; }
 function continueSaved() {}
 function completeManual(value) {}
 function forgetSaved() {}
}''')
    engine.rootContext().setContextProperty("backend",backend)
    engine.rootContext().setContextProperty("themes",themes)
    engine.rootContext().setContextProperty("auth",auth)
    window=create('''import QtQuick
import QtQuick.Window
import BSICustomizer
Window {
 width:1420;height:920;visible:true
 ControlCenterBackdrop { id:ambience; objectName:"testAmbience"; anchors.fill:parent }
 LoadingStrip { objectName:"testLoading"; anchors.top:parent.top; width:parent.width; z:100 }
 AddonsPage { id:page;objectName:"testStudio";anchors.fill:parent }
 LoginGate { id:login;anchors.fill:parent;appWindow:parent;visible:false }
 function choose() { page.selectPack(3); page.edit("name","My custom pack"); }
 function customize() { page.openEditor(); }
 function dismissEditor() { page.closeEditor(); }
 function returnToCarbon() { page.selectPack(0); }
 function returnToCustom() { page.selectPack(3); }
 function stage() { page.stage(); }
 function invalid() { page.paletteJson = "not JSON"; page.stage(); }
 function showLogin() { page.visible=false; login.visible=true; }
}''')
    def named(name):
        # Repeater delegates have visual parents, not necessarily QObject parents.
        pending=[window.contentItem()]
        while pending:
            item=pending.pop()
            if item.objectName()==name: return item
            pending.extend(item.childItems())
        return None
    QTest.qWait(150)
    # Motion has explicit off switches; no engine calls or blocking curtain.
    ambience=window.findChild(QObject,"testAmbience")
    strip=window.findChild(QObject,"testLoading")
    assert ambience.property("moving") is True
    strip.setProperty("busy",True);QTest.qWait(50)
    assert strip.property("animating") is True
    themes.setProperty("reduceMotion",True);QTest.qWait(30)
    assert ambience.property("moving") is False
    assert strip.property("animating") is False
    motion=create("import QtQuick\nimport BSICustomizer\nQtObject { property int duration: Theme.motionDuration }")
    assert motion.property("duration")==0
    themes.setProperty("reduceMotion",False)
    assert motion.property("duration")==150
    themes.setProperty("backgroundMotion",False)
    assert ambience.property("moving") is False
    themes.setProperty("backgroundMotion",True)
    ambience.setProperty("animationAllowed",False)
    assert ambience.property("moving") is False
    ambience.setProperty("animationAllowed",True)
    strip.setProperty("animationAllowed",False)
    assert strip.property("animating") is False
    strip.setProperty("busy",False)
    assert calls()==[],"page creation must not scan or inspect"
    assert named("skinCollection5") is not None
    assert named("selectedDesignInspector") is not None
    if screenshots:
        assert window.grabWindow().save(str(screenshots/"collection-native.png"))
    QMetaObject.invokeMethod(window,"customize"); QTest.qWait(250)
    QMetaObject.invokeMethod(window,"choose");QTest.qWait(50)
    assert named("skinPackName").property("text")=="My custom pack"
    QMetaObject.invokeMethod(window,"dismissEditor"); QTest.qWait(250)
    QMetaObject.invokeMethod(window,"returnToCarbon")
    QMetaObject.invokeMethod(window,"returnToCustom")
    assert named("selectedDesignName").property("text")=="My custom pack", "per-pack drafts survive selection"
    QMetaObject.invokeMethod(window,"stage")
    assert calls()==["queue-skin"]
    QMetaObject.invokeMethod(window,"invalid")
    assert calls()==["queue-skin"],"invalid JSON never reaches the native queue"
    backend.setProperty("exePath","another.exe");QTest.qWait(50)
    for w,h in [(1100,720),(1420,920),(980,640)]:
        window.resize(w,h);QTest.qWait(50)
    QMetaObject.invokeMethod(window,"showLogin");QTest.qWait(50)
    assert named("nativeDiscordLogin") is not None
    assert not errors,errors
    # Full production shell with mock services: no production auth bypass.
    # Only the fixture Auth QtObject changes. All real C++ engines are tested separately.
    updates=create('''import QtQuick
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
    engine.rootContext().setContextProperty("updates",updates)
    backend.setProperty("calls",[])
    shell=create("import BSICustomizer\nMain {}")
    shell.resize(1440,930);QTest.qWait(150)
    loaders=[shell.findChild(QObject,name) for name in ["studioDashboardLoader","featureResources","featureInterface","featureAddons"]]
    assert all(loader is not None for loader in loaders)
    assert all(loader.property("item") is None for loader in loaders), "no locked feature page is instantiated"
    auth.setProperty("authorized",True)
    for _ in range(100):
        QTest.qWait(20)
        if loaders[-1].property("item") is not None: break
    assert loaders[-1].property("item") is not None, "authorized Collection loads on demand"
    assert all(loader.property("item") is None for loader in loaders[:-1]), "other editors stay uncreated"
    assert calls()==[], "shell creation/authorization never triggers scan/inspection"
    QTest.qWait(250)
    if screenshots:
        assert shell.grabWindow().save(str(screenshots/"control-center-native.png"))
    busyStrip=shell.findChild(QObject,"shellLoadingStrip")
    assert busyStrip is not None and not busyStrip.property("busy")
    backend.setProperty("busy",True)
    assert busyStrip.property("busy"), "real backend work drives the nonmodal loading strip"
    QMetaObject.invokeMethod(shell,"navigatePage",Q_ARG("QVariant",11))
    QTest.qWait(100)
    assert shell.property("currentPage")==11, "navigation stays responsive during serialized operations"
    backend.setProperty("busy",False)
    assert not busyStrip.property("busy")
    auth.setProperty("authorized",False);QTest.qWait(100)
    assert all(loader.property("item") is None for loader in loaders), "sign-out unloads all editors"
    assert not errors,errors
    shell.close()
    controls=create('''import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import BSICustomizer
Window {
 width:700;height:150;visible:true
 property int modifications: 0
 RowLayout {
  anchors.centerIn:parent;spacing:10
  StudioTextField { objectName:"formText"; text:"Editable" }
  StudioSpinBox { id:number; objectName:"formNumber"; from:0;to:32;value:8;editable:true; onValueModified:modifications++ }
  StudioComboBox { id:choice;objectName:"formChoice";model:["line","rounded","bold"] }
  StudioCheckBox { id:tick;objectName:"formTick";text:"Motion" }
 }
 function openChoice() { choice.popup.open(); }
 function closeChoice() { choice.popup.close(); }
 function exercise() { number.increase(); choice.currentIndex=2; tick.toggle(); }
}''')
    QTest.qWait(100)
    QMetaObject.invokeMethod(controls,"openChoice");QTest.qWait(180)
    QMetaObject.invokeMethod(controls,"closeChoice");QTest.qWait(180)
    QMetaObject.invokeMethod(controls,"exercise");QTest.qWait(50)
    assert controls.findChild(QObject,"formNumber").property("value")==9
    assert controls.findChild(QObject,"formChoice").property("currentIndex")==2
    assert controls.findChild(QObject,"formTick").property("checked") is True
    if screenshots:
        assert controls.grabWindow().save(str(screenshots/"native-form-controls.png"))
    controls.close()
    assert not errors,errors
    # Avoid leaving QML contexts bound to dead Python objects during teardown.
    window.close();objects.clear();engine.deleteLater();QTest.qWait(10)
print("PASS: native bento/inspector, editable retained drafts, safe staging, resize, login, lazy locked/authenticated shell, responsive busy navigation and sign-out teardown")
