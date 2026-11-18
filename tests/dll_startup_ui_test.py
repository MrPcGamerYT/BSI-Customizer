"""Exercise the real opt-in native panel with explicit mock services, not a player."""

import os
from pathlib import Path
import shutil
import tempfile

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QSG_RHI_BACKEND", "software")
from PySide6.QtCore import QObject, QMetaObject, QUrl
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlComponent, QQmlEngine
from PySide6.QtTest import QTest

app = QGuiApplication([])
source = Path(__file__).resolve().parents[1] / "src/qml"
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
        c = QQmlComponent(engine)
        components.append(c)
        c.setData(text.encode(), QUrl.fromLocalFile(str(Path(directory) / "host.qml")))
        obj = c.create()
        assert obj is not None, [e.toString() for e in c.errors()]
        QQmlEngine.setObjectOwnership(obj, QQmlEngine.CppOwnership)
        objects.append(obj)
        return obj

    backend = create(
        """import QtQuick
QtObject {
 property bool busy: false
 property string exePath: "fixture/HD-Player.exe"
 property var calls: []
 property bool installed: false
 property bool missingDll: false
 signal workspaceChanged()
 function dllStartupInspect(name) {
  calls = calls.concat(["inspect:" + name]); workspaceChanged();
  if (missingDll) return {ok:false,canRestore:installed,msg:"DLL missing; restore is available"};
  return {ok:true,canApply:!installed,alreadyApplied:installed,canRestore:installed,
          exeSha256:"reviewed-exe",dllSha256:"reviewed-dll",warning:"Explicit fixture risks",
          dllDirectImports:["KERNEL32.dll"],msg:"Fixture inspection"};
 }
 function dllStartupApply(name,exe,dll,ack) {
  if (exe!=="reviewed-exe" || dll!=="reviewed-dll" || ack!==true) throw Error("Unreviewed apply");
  calls = calls.concat(["apply:" + name]); installed=true; workspaceChanged();
  return {ok:true,changed:true,msg:"Fixture applied"};
 }
 function dllStartupRestore() {
  calls = calls.concat(["restore"]); installed=false; workspaceChanged();
  return {ok:true,changed:true,msg:"Fixture restored"};
 }
}"""
    )
    themes = create(
        """import QtQuick
QtObject {property bool reduceMotion:true; property bool backgroundMotion:false; property int theme:0; property int effectiveTheme:0; property color accentColor:"#7c5cff"}"""
    )
    engine.rootContext().setContextProperty("backend", backend)
    engine.rootContext().setContextProperty("themes", themes)
    window = create(
        """import QtQuick
import QtQuick.Controls.Basic
import BSICustomizer
ApplicationWindow {
 width:900;height:720;visible:true
 ScrollView { anchors.fill:parent; contentWidth:availableWidth
  DllStartupPanel { id:panel; width:parent.width }
 }
 function attemptApply() { panel.applyReviewed(); }
}"""
    )

    def named(name):
        obj = window.findChild(QObject, name)
        if obj is None:
            obj = engine.findChild(QObject, name)
        assert obj is not None, name
        return obj

    def invoke(obj, method):
        assert QMetaObject.invokeMethod(obj, method), method
        QTest.qWait(30)

    def calls():
        value = backend.property("calls")
        return value.toVariant() if hasattr(value, "toVariant") else value

    QTest.qWait(100)
    inspect, apply, restore = [
        named(n) for n in ("inspectStartupDll", "applyStartupDll", "restoreStartupDll")
    ]
    assert calls() == [], "no file reads, writes or DLL execution on entry"
    assert named("startupDllName").property("text") == "zlag.dll"
    assert not apply.property("enabled") and not restore.property("enabled")
    invoke(window, "attemptApply")
    assert calls() == [], "controller must refuse an unreviewed apply too"
    invoke(inspect, "clicked")
    assert calls() == ["inspect:zlag.dll"] and apply.property("enabled")
    invoke(apply, "clicked")
    confirm = named("confirmStartupDllApply")
    assert not confirm.property("enabled"), "acknowledgement must default OFF on every opening"
    invoke(named("startupDllConfirm"), "reject")
    assert not any(c.startswith("apply:") for c in calls()), "cancel is read-only"
    invoke(apply, "clicked")
    named("startupDllAcknowledge").setProperty("checked", True)
    QTest.qWait(30)
    assert confirm.property("enabled")
    invoke(confirm, "clicked")
    assert "apply:zlag.dll" in calls() and restore.property("enabled") and not apply.property("enabled")
    assert named("startupDllReport").property("text") == "Fixture applied"
    backend.setProperty("missingDll", True)
    invoke(inspect, "clicked")
    assert restore.property("enabled"), "DLL deletion must not trap user without a restore button"
    invoke(restore, "clicked")
    # Dialog acceptance invokes only the guarded restore controller.
    panel = named("dllStartupPanel")
    invoke(panel, "restoreReviewed")
    assert "restore" in calls() and not backend.property("installed")
    backend.setProperty("missingDll", False)
    invoke(inspect, "clicked")
    named("startupDllName").setProperty("text", "other.dll")
    QTest.qWait(30)
    assert not apply.property("enabled"), "filename changes invalidate a review"
    invoke(inspect, "clicked")
    assert apply.property("enabled")
    backend.setProperty("exePath", "another/HD-Player.exe")
    QTest.qWait(30)
    assert not apply.property("enabled"), "EXE selection changes invalidate a review"
    invoke(inspect, "clicked")
    backend.setProperty("busy", True)
    QTest.qWait(30)
    assert (
        not inspect.property("enabled") and not apply.property("enabled") and not restore.property("enabled")
    )
    backend.setProperty("busy", False)
    invoke(backend, "workspaceChanged")
    assert not apply.property("enabled"), "other native operations invalidate a reviewed plan"
    errors = [
        w for w in warnings if any(s in w for s in ("Error", "is not defined", "Cannot", "Binding loop"))
    ]
    assert not errors, errors
    window.close()
    del objects[:]
print(
    "PASS: native startup-DLL panel; no entry work; review + explicit acknowledgement; cancel; apply/restore; missing DLL recovery; stale names/paths/workspace; busy guard"
)
