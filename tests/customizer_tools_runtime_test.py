"""Real Qt Quick UX tests with mock engines, never an auth/patch bypass."""
import os
import shutil
import tempfile
from pathlib import Path
os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
os.environ.setdefault('QSG_RHI_BACKEND', 'software')
from PySide6.QtCore import QObject, QUrl, QMetaObject, Q_ARG
from PySide6.QtGui import QGuiApplication, QWindow
from PySide6.QtQml import QQmlEngine, QQmlComponent
from PySide6.QtTest import QTest
app = QGuiApplication([])
source = Path(__file__).resolve().parents[1] / 'src/qml'
with tempfile.TemporaryDirectory() as temp:
    module = Path(temp) / 'BSICustomizer'; shutil.copytree(source, module)
    (module / 'qmldir').write_text('\n'.join(['module BSICustomizer', 'singleton Theme 1.0 Theme.qml'] + [f'{p.stem} 1.0 {p.relative_to(module).as_posix()}' for p in module.rglob('*.qml') if p.name != 'Theme.qml']))
    engine = QQmlEngine(); engine.addImportPath(temp)
    warnings = []; engine.warnings.connect(lambda es: warnings.extend(e.toString() for e in es))
    components = []; objects = []
    def create(text):
        c = QQmlComponent(engine); c.setParent(engine); components.append(c)
        c.setData(text.encode(), QUrl.fromLocalFile(str(Path(temp)/'fixture.qml')))
        o = c.create(); assert o is not None, [e.toString() for e in c.errors()]
        QQmlEngine.setObjectOwnership(o, QQmlEngine.CppOwnership)
        if not isinstance(o, QWindow): o.setParent(engine)
        objects.append(o); return o
    def invoke(o, method, *args):
        assert QMetaObject.invokeMethod(o, method, *[Q_ARG('QVariant', a) for a in args])
        QTest.qWait(30)
    def val(o, key):
        value = o.property(key)
        return value.toVariant() if hasattr(value, 'toVariant') else value
    themes = create('''import QtQuick
QtObject { property bool reduceMotion: true; property bool backgroundMotion: false; property int effectiveTheme: 0; property color accentColor: "#7c5cff" }''')
    engine.rootContext().setContextProperty('themes', themes)
    backend = create('''import QtQuick
QtObject {
 property bool busy: false
 property string exePath: "C:/One/HD-Player.exe"
 property var resourceInfos: []
 property var customizationProfile: ({name:"Fixture"})
 property var workspaceOperations: [{kind:"theme",edits:{base:"#000000"}}]
 property var calls: []
 signal workspaceChanged()
 function localFilePath(url) { return String(url); }
 function workspace(action, options) {
  calls=calls.concat([{action:action,options:options}]);
  if(action==="history") return {ok:true,msg:"Mock history",entries:[
    {id:"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",path:"C:/One/HD-Player.exe",state:"applied",time:"2026-10-25",before:"old",after:"new",operationCount:1,operations:[{kind:"theme"}]},
    {id:"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",path:"C:/Two/HD-Player.exe",state:"prepared",time:"2026-10-24",before:"old",after:"new",operationCount:1}
  ]};
  return {ok:true,msg:"Mock action"};
 }
}''')
    engine.rootContext().setContextProperty('backend', backend)
    host = create('''import QtQuick
import QtQuick.Window
import BSICustomizer
Window {
 width:1100;height:800;visible:true
 property int destination: -1
 property int settingsTab: -1
 ToolFinder { id:finder;authorized:true; onNavigate:(route)=>destination=route;onSettings:(tab)=>settingsTab=tab }
 BackupsPage { id:recovery;objectName:"recoveryPage";anchors.fill:parent }
 function openFinder(){finder.open();}
 function lockFinder(){finder.authorized=false;}
 function openReview(){recovery.reviewRestore();}
}''')
    QTest.qWait(80)
    assert val(backend, 'calls') == [], 'opening Recovery does not read files'
    finder = host.findChild(QObject, 'toolFinder'); assert finder
    invoke(host, 'openFinder')
    query = finder.findChild(QObject, 'toolFinderQuery'); assert query
    query.setProperty('text', 'round corners'); QTest.qWait(30)
    invoke(finder, 'choose', 0)
    assert host.property('destination') == 8
    invoke(host, 'openFinder'); query.setProperty('text', 'reduced motion'); QTest.qWait(30)
    invoke(finder, 'choose', 0)
    assert host.property('settingsTab') == 0
    invoke(host, 'openFinder'); invoke(host, 'lockFinder')
    assert not finder.property('visible')
    invoke(finder, 'choose', 0)
    assert host.property('destination') == 8
    assert val(backend, 'calls') == [], 'searching/choosing tools never calls an engine'
    recovery = host.findChild(QObject, 'recoveryPage')
    invoke(recovery, 'refresh')
    assert len(val(recovery, 'filteredEntries')) == 1
    invoke(recovery, 'reviewRestore')
    dialog = recovery.findChild(QObject, 'recoveryConfirmDialog'); assert dialog.property('visible')
    invoke(dialog, 'reject')
    assert len(val(backend, 'calls')) == 1, 'cancel must not restore'
    invoke(recovery, 'reviewRestore')
    backend.setProperty('exePath', 'C:/Two/HD-Player.exe'); QTest.qWait(40)
    assert not dialog.property('visible') and val(recovery, 'entries') == []
    invoke(dialog, 'accept')
    assert len(val(backend, 'calls')) == 1, 'target change cancels a stale restore'
    invoke(recovery, 'refresh'); invoke(recovery, 'reviewRestore'); invoke(dialog, 'accept')
    calls = val(backend, 'calls')
    assert calls[-1] == {'action':'restore','options':{'id':'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'}}
    assert not recovery.property('loaded'), 'successful restore invalidates displayed history'
    print("UX progress: search and recovery assertions passed", flush=True)
    host.close()
    # Instantiate the real Workspace page, then exercise its confirmation guard.
    backend.setProperty('calls', [])
    workspace_host = create('''import QtQuick
import QtQuick.Window
import BSICustomizer
Window { width:1100;height:800;visible:true
 WorkspacePage { id:workspace;objectName:"workspacePage";anchors.fill:parent }
 function prepare(){workspace.plan={ok:true,sha256:"before",outputSha256:"after",bytesBefore:1024,bytesAfter:2048,steps:[{ok:true,msg:"Mock verified"}]};}
}''')
    print("UX progress: Workspace created", flush=True)
    QTest.qWait(120)
    workspace = workspace_host.findChild(QObject, 'workspacePage')
    review = workspace.findChild(QObject, 'workspaceReviewDialog')
    acknowledged = workspace.findChild(QObject, 'workspaceReviewAcknowledged')
    apply = workspace.findChild(QObject, 'workspaceConfirmApply')
    assert val(backend, 'calls') == [], 'opening Workspace never scans or applies'
    invoke(workspace_host, 'prepare'); invoke(review, 'open')
    assert not apply.property('enabled')
    invoke(review, 'accept')
    assert val(backend, 'calls') == [], 'programmatic acceptance cannot bypass acknowledgment'
    invoke(review, 'open'); acknowledged.setProperty('checked', True); QTest.qWait(20)
    assert apply.property('enabled')
    backend.setProperty('exePath', 'C:/Changed/HD-Player.exe'); QTest.qWait(30)
    assert not review.property('visible')
    invoke(review, 'accept'); assert val(backend, 'calls') == [], 'stale target cannot apply'
    invoke(workspace_host, 'prepare'); invoke(review, 'open'); acknowledged.setProperty('checked', True)
    invoke(backend, 'workspaceChanged')
    assert not review.property('visible')
    invoke(review, 'accept'); assert val(backend, 'calls') == [], 'queue revision invalidates review'
    invoke(workspace_host, 'prepare'); invoke(review, 'open'); acknowledged.setProperty('checked', True)
    invoke(review, 'accept')
    assert val(backend, 'calls') == [{'action':'apply','options':{'outputSha256':'after'}}]
    print("UX progress: all action assertions passed", flush=True)
    assert not warnings, warnings
    workspace_host.close(); objects.clear(); engine.deleteLater(); QTest.qWait(10)
    print("UX progress: Qt teardown completed", flush=True)
print('PASS: real Qt tool finder, locked navigation, explicit history, cancel/target-safe restores, acknowledged/frozen batch review and no entry scans (mock engines).')
