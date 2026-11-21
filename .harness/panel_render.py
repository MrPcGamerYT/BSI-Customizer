"""Render the native Player-interface page with the Settings-panel rebuild tab
selected, so the new section can be eyeballed against the rest of the studio."""
import os, shutil, sys, tempfile
from pathlib import Path
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QSG_RHI_BACKEND", "software")
from PySide6.QtCore import QObject, QUrl, Qt
from PySide6.QtGui import QGuiApplication, QFontDatabase, QFont, QWindow
from PySide6.QtQml import QQmlEngine, QQmlComponent
from PySide6.QtQuick import QQuickWindow
from PySide6.QtTest import QTest

REPO = Path(__file__).resolve().parents[1]
app = QGuiApplication([])
for name in ("Inter.ttf", "JetBrainsMono.ttf"):
    QFontDatabase.addApplicationFont(str(REPO / "src/qml/fonts" / name))
app.setFont(QFont("Inter"))
out = REPO / (sys.argv[1] if len(sys.argv) > 1 else ".harness/renders/native-panel-section.png")
out.parent.mkdir(parents=True, exist_ok=True)

with tempfile.TemporaryDirectory() as d:
    m = Path(d) / "BSICustomizer"; shutil.copytree(REPO / "src/qml", m)
    entries = ["module BSICustomizer", "singleton Theme 1.0 Theme.qml"] + [
        f"{p.stem} 1.0 {p.relative_to(m).as_posix()}" for p in m.rglob("*.qml") if p.name != "Theme.qml"]
    (m / "qmldir").write_text("\n".join(entries))
    eng = QQmlEngine(); eng.addImportPath(d)
    comps = []
    def create(q):
        c = QQmlComponent(eng); c.setParent(eng); comps.append(c)
        c.setData(q.encode(), QUrl.fromLocalFile(str(Path(d) / "t.qml")))
        o = c.create(); assert o is not None, [e.toString() for e in c.errors()]
        QQmlEngine.setObjectOwnership(o, QQmlEngine.CppOwnership)
        if not isinstance(o, QWindow):
            o.setParent(eng)
        return o
    backend = create('''import QtQuick
QtObject {
 property string exePath: "C:/Program Files/BlueStacks_nxt/HD-Player.exe"; property bool busy: false
 property var resourceInfos: []; property var themeEntries: []; property var workspaceOperations: []
 property var customizationProfile: ({})
 signal log(string message, string color)
 function settingsPanelRebuildState() { return {
   supported: true, applied: false, restorable: true, settingsPageCount: 7,
   settingsQmlResourceCount: 12, compiledUnitsDetected: 2, compiledUnitsActive: 0,
   embeddedQmlResourceCount: 34,
   qmlResourceSamples: ["plr.qml", "UiSettingsWindow.qml", "UiThemeObject.qml", "SettingsGraphics.qml"],
   tabs: [ {id:"graphics",page:"SettingsGraphics.qml",label:"Graphics",index:0},
           {id:"display",page:"SettingsDisplay.qml",label:"Display",index:1},
           {id:"cpu",page:"SettingsCpu.qml",label:"CPU allocation",index:2} ],
   logoFallback: true, logoStatus: "", detail: "Editable player QML sources found." }; }
 function saveSettingsPanelProfile(a,b,c){ return {ok:true,msg:"Saved."}; }
 function setSettingsPanelAsset(a,b,c){ return {ok:true,msg:"Saved."}; }
 function clearSettingsPanelAsset(a,b){ return {ok:true,msg:"Saved."}; }
 function applySettingsPanelRebuild(e){ return {ok:true,msg:"Applied."}; }
 function layoutTweaks(){return [];}
 function sidebarButtonRows(){return {supported:false,applied:false,buttons:[],detail:""};}
 function aboutPageState(){return {supported:false,applied:false,detail:"",text:"",links:[]};}
 function localFilePath(u){return String(u);}
}''')
    themes = create('''import QtQuick
QtObject { property bool reduceMotion: true; property bool backgroundMotion: false;
           property int effectiveTheme: 0; property color accentColor: "#7c5cff" }''')
    settings = create('''import QtQuick
QtObject { property string aboutDeveloperName: "Mr.Pc Gamer & Romulos"
           property string aboutTagline: "BlueStacks tools · with contributor Nova"
           property var aboutLinks: []; property var settingsPanelProfile: ({tabIcons:{},hiddenTabs:[]})
           function openExeFolder(){} }''')
    eng.rootContext().setContextProperty("backend", backend)
    eng.rootContext().setContextProperty("themes", themes)
    eng.rootContext().setContextProperty("settings", settings)
    win = create('''import QtQuick
import QtQuick.Window
import BSICustomizer
Window { visible: true; width: 1420; height: 1120; color: Theme.window
  UiPage { id: page; anchors.fill: parent }
  Component.onCompleted: page.currentSection = 4
}''')
    QTest.qWait(600)
    shot = QQuickWindow.grabWindow(win)
    shot.save(str(out))
    shot.save(str(out))
    print("saved", out, shot.size())
