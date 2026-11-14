"""Real Qt Quick Skin Studio/login creation with an explicit mock backend.
This does not execute a player or certify native patching. PySide6 6.8.3 required.
"""
import os
import shutil
import tempfile
from pathlib import Path
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QSG_RHI_BACKEND", "software")
from PySide6.QtCore import QObject, QUrl, QMetaObject
from PySide6.QtGui import QGuiApplication, QWindow
from PySide6.QtQml import QQmlEngine, QQmlComponent
from PySide6.QtTest import QTest
app=QGuiApplication([])
source=Path(__file__).resolve().parents[1]/"src/qml"
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
    backend=create('''import QtQuick
QtObject {
 property bool busy: false
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
QtObject { property int effectiveTheme: 0; property color accentColor: "#d8faec" }''')
    auth=create('''import QtQuick
QtObject {
 property bool busy: false
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
 AddonsPage { id:page;objectName:"testStudio";anchors.fill:parent }
 LoginGate { id:login;anchors.fill:parent;appWindow:parent;visible:false }
 function choose() { page.selectPack(3); page.edit("name","My custom pack"); }
 function stage() { page.stage(); }
 function invalid() { page.paletteJson = "not JSON"; page.stage(); }
 function showLogin() { page.visible=false; login.visible=true; }
}''')
    QTest.qWait(150)
    assert backend.property("calls").toVariant()==[],"page creation must not scan or inspect"
    assert window.findChild(QObject,"skinCollection5") is not None
    QMetaObject.invokeMethod(window,"choose");QTest.qWait(50)
    assert window.findChild(QObject,"skinPackName").property("text")=="My custom pack"
    QMetaObject.invokeMethod(window,"stage")
    assert backend.property("calls").toVariant()==["queue-skin"]
    QMetaObject.invokeMethod(window,"invalid")
    assert backend.property("calls").toVariant()==["queue-skin"],"invalid JSON never reaches the native queue"
    backend.setProperty("exePath","another.exe");QTest.qWait(50)
    for w,h in [(1100,720),(1420,920),(980,640)]:
        window.resize(w,h);QTest.qWait(50)
    QMetaObject.invokeMethod(window,"showLogin");QTest.qWait(50)
    assert window.findChild(QObject,"nativeDiscordLogin") is not None
    assert not errors,errors
    # Avoid leaving QML contexts bound to dead Python objects during teardown.
    window.close();objects.clear();engine.deleteLater();QTest.qWait(10)
print("PASS: native six-card studio, editable name, no automatic operations, invalid-JSON refusal, resize and Discord login QML creation")
