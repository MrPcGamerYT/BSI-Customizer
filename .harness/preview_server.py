"""Live preview of the real native UI.

Renders the actual QML (Main.qml + every page) with Qt 6 offscreen against
explicit mock services, then serves the frames as a clickable gallery on
http://0.0.0.0:<port>. Nothing here touches an emulator, an executable or the
network: it is a visual harness for the UI itself.

    python .harness/preview_server.py [port]
"""
import http.server
import json
import os
import shutil
import socketserver
import sys
import tempfile
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QSG_RHI_BACKEND", "software")

from PySide6.QtCore import Q_ARG, QBuffer, QIODevice, QMetaObject, QUrl
from PySide6.QtGui import QFont, QFontDatabase, QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine, QQmlComponent, QQmlEngine
from PySide6.QtQuick import QQuickWindow
from PySide6.QtTest import QTest

REPO = Path(__file__).resolve().parents[1]
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
WIDTH, HEIGHT = 1440, 900

PAGES = [
    ("home", "Overview", 0), ("theme", "Color studio", 1), ("resources", "Resources", 2),
    ("fps", "Performance", 3), ("integrity", "Integrity & safety", 4), ("boot", "Boot experience", 5),
    ("interface", "Player interface", 6), ("rpcid", "Discord presence", 7), ("gif", "Animated presence", 8),
    ("workspace", "Patch workspace", 9), ("debloat", "Debloat & disk manager", 10),
    ("backups", "Backups", 11), ("addons", "Add-ons", 12),
]
SECTIONS = [
    ("layout", "Layout & clean-up", 0), ("about", "About preview & editor", 1),
    ("remove", "Remove items", 2), ("appearance", "Appearance studio", 3),
    ("panel", "Settings panel", 4),
]

BACKEND = '''
import QtQuick
QtObject {
    property string exePath: "C:/Program Files/BlueStacks_nxt/HD-Player.exe"
    property bool busy: false
    property var installs: [ {profile: "BlueStacks 5 (Nougat 32-bit)", exe: "C:/Program Files/BlueStacks_nxt/HD-Player.exe"} ]
    property var resourceInfos: []
    property var themeEntries: []
    property var workspaceOperations: []
    property var customizationProfile: ({})
    property var log: ""
    signal logMessage(string message, string color)
    function settingsPanelRebuildState() {
        return { supported: true, applied: false, restorable: true,
                 settingsPageCount: 7, settingsQmlResourceCount: 12,
                 compiledUnitsDetected: 2, compiledUnitsActive: 0,
                 embeddedQmlResourceCount: 34,
                 qmlResourceSamples: ["plr.qml", "UiSettingsWindow.qml", "UiThemeObject.qml", "SettingsGraphics.qml"],
                 tabs: [ {id:"graphics",page:"SettingsGraphics.qml",label:"Graphics",index:0},
                         {id:"display",page:"SettingsDisplay.qml",label:"Display",index:1},
                         {id:"cpu",page:"SettingsCpu.qml",label:"CPU allocation",index:2} ],
                 logoFallback: true, logoStatus: "", detail: "Editable player QML sources found." };
    }
    function saveSettingsPanelProfile(a, b, c, d) { return {ok:true, msg:"Settings-panel options saved on this PC."}; }
    function setSettingsPanelAsset(a, b, c) { return {ok:true, msg:"The safe local image is saved in the profile."}; }
    function clearSettingsPanelAsset(a, b) { return {ok:true, msg:"Custom artwork cleared."}; }
    function applySettingsPanelRebuild(e) { return {ok:true, msg: e ? "The compact, draggable Settings window was applied." : "The original Settings-window sources were restored."}; }
    function layoutTweaks() { return []; }
    function sidebarButtonRows() { return {supported:false, applied:false, buttons:[], detail:""}; }
    function aboutPageState() { return {supported:false, applied:false, detail:"", text:"", links:[]}; }
    function removalNames() { return []; }
    function localFilePath(u) { return String(u); }
    function detectInstalls() { return []; }
    function useInstall(i) {}
    function restoreBackup(p) {}
    function purgePlayerCache() { return {ok:true}; }
    function themePresets() { return []; }
    function resolvedThemeMethod() { return "auto"; }
    function gradientTargets() { return []; }
}
'''

AUTH = '''
import QtQuick
QtObject {
    property bool authorized: true
    property bool busy: false
    property var savedUser: ({DisplayName: "Preview"})
    property string savedMeta: ""
    property bool hasSavedSession: true
    property real progress: 0
    property bool progressIndeterminate: false
    property string note: ""
    property string noteKind: ""
    property string inviteUrl: ""
    property string setupHint: ""
    function signOut() {}
    function startDiscord() { return {}; }
    function continueSaved() {}
    function completeManual(v) {}
    function forgetSaved() {}
}
'''

THEMES = '''
import QtQuick
QtObject {
    property bool reduceMotion: true
    property bool backgroundMotion: true
    property int theme: 0
    property int effectiveTheme: 0
    property color accentColor: "#7c5cff"
}
'''

SETTINGS = '''
import QtQuick
QtObject {
    property string aboutDeveloperName: "Mr.Pc Gamer & Romulos"
    property string aboutTagline: "BlueStacks tools · with contributor Nova"
    property var aboutLinks: []
    property var settingsPanelProfile: ({ tabIcons: {}, hiddenTabs: [] })
    function openExeFolder() {}
}
'''

SYSINFO = '''
import QtQuick
QtObject { property string os: "Windows 11"; property string cpu: "Ryzen 7"; property string ram: "16 GB" }
'''

UPDATES = '''
import QtQuick
QtObject {
    property bool available: false
    property string currentVersion: "4.0.1"
    property bool busy: false
    property string state: ""
    property string note: ""
    property bool readyToInstall: false
    property bool autoCheck: true
    property real progress: 0
    property string latestVersion: ""
    property string releaseNotes: ""
}
'''


def render_all():
    """Render every page and every Player-interface section to PNG bytes."""
    app = QGuiApplication.instance() or QGuiApplication([])
    for name in ("Inter.ttf", "JetBrainsMono.ttf"):
        QFontDatabase.addApplicationFont(str(REPO / "src/qml/fonts" / name))
    app.setFont(QFont("Inter"))
    directory = Path(tempfile.mkdtemp(prefix="bsi-preview-"))
    module = directory / "BSICustomizer"
    shutil.copytree(REPO / "src/qml", module)
    (module / "qmldir").write_text("\n".join(
        ["module BSICustomizer", "singleton Theme 1.0 Theme.qml"]
        + [f"{p.stem} 1.0 {p.relative_to(module).as_posix()}"
           for p in module.rglob("*.qml") if p.name != "Theme.qml"]))

    engine = QQmlApplicationEngine()
    engine.addImportPath(str(directory))
    # The mocks MUST be created by this same engine: a QML object built in
    # another engine cannot hand its JS objects (QVariantMap results, nested
    # arrays) back across the boundary, and every map then reads as empty.
    kept = []

    def mock(source):
        component = QQmlComponent(engine)
        component.setParent(engine)
        component.setData(source.encode(), QUrl.fromLocalFile(str(directory / "mock.qml")))
        obj = component.create()
        assert obj is not None, [e.toString() for e in component.errors()]
        QQmlEngine.setObjectOwnership(obj, QQmlEngine.CppOwnership)
        obj.setParent(engine)
        kept.append((component, obj))
        return obj

    for key, source in (("backend", BACKEND), ("auth", AUTH), ("themes", THEMES),
                        ("settings", SETTINGS), ("sysinfo", SYSINFO), ("updates", UPDATES)):
        engine.rootContext().setContextProperty(key, mock(source))
    engine.load(QUrl.fromLocalFile(str(module / "Main.qml")))
    assert engine.rootObjects(), "Main.qml failed to load"
    window = engine.rootObjects()[0]
    QTest.qWait(600)
    # The offscreen platform reports an 800x600 screen, so the window shrinks
    # itself on load; give it a real desktop size before rendering.
    window.setProperty("minimumWidth", 900)
    window.setProperty("minimumHeight", 600)
    window.setProperty("width", WIDTH)
    window.setProperty("height", HEIGHT)
    QTest.qWait(400)

    def grab():
        image = QQuickWindow.grabWindow(window)
        buffer = QBuffer()
        buffer.open(QIODevice.WriteOnly)
        image.save(buffer, "PNG")
        return bytes(buffer.data())

    frames = {}
    QMetaObject.invokeMethod(window, "navigatePage", Q_ARG("QVariant", 6))
    QTest.qWait(900)
    page = None
    pending = [window.contentItem()]
    while pending:
        item = pending.pop()
        if item.objectName() == "featureInterface":
            page = item
            pending = []
            continue
        pending.extend(item.childItems())
    assert page is not None, "Player interface page not found"

    def find(root, name):
        pending = [root]
        while pending:
            item = pending.pop()
            if item.objectName() == name:
                return item
            pending.extend(item.childItems())
        return None

    inner = page.property("item")
    bar = find(inner, "playerUiSections")
    for slug, _label, index in SECTIONS:
        QMetaObject.invokeMethod(bar, "setCurrentIndex", Q_ARG("int", index))
        QTest.qWait(500)
        frames[f"interface::{slug}"] = grab()
    for slug, _label, index in PAGES:
        QMetaObject.invokeMethod(window, "navigatePage", Q_ARG("QVariant", index))
        QTest.qWait(800)
        frames[slug] = grab()
    return frames


HTML = """<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>BSI Customizer — live UI preview</title>
<style>
  :root { --bg:#0a0a0e; --card:#111116; --raised:#17171d; --line:#2a2a33;
          --text:#f2f2f5; --dim:#8a8a96; --accent:#7c5cff; }
  * { box-sizing:border-box; }
  body { margin:0; background:var(--bg); color:var(--text);
         font:14px/1.45 Inter,system-ui,-apple-system,"Segoe UI",sans-serif; }
  header { display:flex; align-items:center; gap:14px; padding:16px 22px;
           border-bottom:1px solid var(--line); position:sticky; top:0;
           background:rgba(10,10,14,.94); backdrop-filter:blur(8px); z-index:5; }
  .mark { width:33px; height:29px; border:1px solid var(--line); border-radius:7px;
          display:grid; place-items:center; font:600 9px/1 Inter; letter-spacing:.5px;
          color:var(--dim); }
  h1 { font-size:15px; margin:0; font-weight:600; letter-spacing:-.2px; }
  header p { margin:2px 0 0; color:var(--dim); font-size:11px; }
  .pill { margin-left:auto; display:flex; align-items:center; gap:6px;
          border:1px solid #24c8e86a; background:#0cc8e86a; color:#c8e86a;
          border-radius:14px; padding:5px 11px; font:600 9px/1 Inter;
          letter-spacing:.8px; text-transform:uppercase; }
  .pill i { width:4px; height:4px; border-radius:2px; background:#c8e86a; }
  main { display:grid; grid-template-columns:206px minmax(0,1fr); gap:22px;
         padding:22px; align-items:start; }
  nav { position:sticky; top:76px; display:flex; flex-direction:column; gap:2px; }
  nav button { all:unset; cursor:pointer; padding:9px 12px; border-radius:9px;
               color:var(--dim); font-size:12.5px; }
  nav button:hover { background:#7c5cff14; color:var(--text); }
  nav button[aria-current="true"] { background:var(--raised); color:var(--text);
        font-weight:600; box-shadow:inset 2px 0 0 var(--accent); }
  nav .group { margin:14px 0 4px; padding-left:12px; font:600 9px/1 Inter;
        letter-spacing:1.2px; color:#5c5c68; text-transform:uppercase; }
  figure { margin:0; }
  .frame { border:1px solid var(--line); border-radius:14px; overflow:hidden;
           background:var(--card); box-shadow:0 24px 60px #0008; }
  .frame img { display:block; width:100%; height:auto; }
  figcaption { display:flex; gap:10px; align-items:center; margin:12px 2px 0;
               color:var(--dim); font-size:12px; flex-wrap:wrap; }
  .tabs { display:flex; gap:6px; flex-wrap:wrap; margin-left:auto; }
  .tabs button { all:unset; cursor:pointer; padding:6px 11px; border-radius:8px;
                 border:1px solid var(--line); color:var(--dim); font-size:11.5px; }
  .tabs button:hover { border-color:#7c5cff88; color:var(--text); }
  .tabs button[aria-current="true"] { background:var(--raised); color:var(--text);
                 border-color:#7c5cff66; font-weight:600; }
  .note { color:#5c5c68; font-size:11.5px; margin:18px 2px 0; }
</style></head>
<body>
<header>
  <div class="mark">BSI</div>
  <div><h1>BSI Customizer</h1><p>Live preview — the real Qt Quick UI, rendered offscreen</p></div>
  <div class="pill"><i></i>Live</div>
</header>
<main>
  <nav id="pages"></nav>
  <figure>
    <div class="frame"><img id="shot" alt="Rendered application view"></div>
    <figcaption><span id="caption"></span><div class="tabs" id="tabs"></div></figcaption>
    <p class="note" id="note"></p>
  </figure>
</main>
<script>
const PAGES = __PAGES__;
const SECTIONS = __SECTIONS__;
const note = document.getElementById('note');
let current = location.hash.replace('#', '') || 'home';
let section = 'layout';

function paint() {
  const page = PAGES.find(p => p.slug === current) || PAGES[0];
  current = page.slug;
  const key = page.slug === 'interface' ? `interface::${section}` : page.slug;
  document.getElementById('shot').src = `/frame/${key}.png?t=${Date.now()}`;
  document.getElementById('caption').textContent =
    page.slug === 'interface'
      ? `Player interface · ${(SECTIONS.find(s => s.slug === section) || SECTIONS[0]).label}`
      : page.label;
  document.querySelectorAll('#pages button').forEach(b =>
    b.setAttribute('aria-current', b.dataset.slug === current));
  const tabs = document.getElementById('tabs');
  tabs.innerHTML = '';
  if (page.slug === 'interface') {
    for (const s of SECTIONS) {
      const b = document.createElement('button');
      b.textContent = s.label;
      b.setAttribute('aria-current', s.slug === section);
      b.onclick = () => { section = s.slug; paint(); };
      tabs.appendChild(b);
    }
  }
  location.hash = current;
}
const nav = document.getElementById('pages');
for (const page of PAGES) {
  if (page.group) {
    const g = document.createElement('div');
    g.className = 'group'; g.textContent = page.group; nav.appendChild(g);
    continue;
  }
  const b = document.createElement('button');
  b.textContent = page.label; b.dataset.slug = page.slug;
  b.onclick = () => { current = page.slug; paint(); };
  nav.appendChild(b);
}
note.textContent = 'Screenshots are produced by rendering Main.qml and every page with Qt 6.8 '
  + 'against mock services — no emulator, no EXE and no network access is involved.';
paint();
</script>
</body></html>
"""


def build_html():
    pages = [{"group": "HOME"}, {"slug": "home", "label": "Overview"},
             {"group": "PATCHER"}]
    for slug, label, _index in PAGES[1:6]:
        pages.append({"slug": slug, "label": label})
    pages.append({"group": "PRESENCE"})
    for slug, label, _index in PAGES[7:9]:
        pages.append({"slug": slug, "label": label})
    pages.append({"group": "TOOLS"})
    for slug, label, _index in PAGES[9:11]:
        pages.append({"slug": slug, "label": label})
    pages.append({"group": "RECOVERY"})
    pages.append({"slug": "backups", "label": "Backups"})
    pages.append({"group": "COLLECTION"})
    pages.append({"slug": "addons", "label": "Add-ons"})
    return (HTML.replace("__PAGES__", json.dumps(pages))
                .replace("__SECTIONS__", json.dumps(
                    [{"slug": s, "label": l} for s, l, _i in SECTIONS])))


def main():
    frames = render_all()
    html = build_html().encode()

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            path = self.path.split("?")[0]
            if path in ("/", "/index.html"):
                body, kind = html, "text/html; charset=utf-8"
            elif path.startswith("/frame/") and path.endswith(".png"):
                body = frames.get(path[len("/frame/"):-len(".png")])
                kind = "image/png"
                if body is None:
                    self.send_error(404)
                    return
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *_args):
            pass

    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.ThreadingTCPServer(("0.0.0.0", PORT), Handler) as httpd:
        print(f"BSI UI preview on http://0.0.0.0:{PORT} ({len(frames)} frames)", flush=True)
        httpd.serve_forever()


if __name__ == "__main__":
    main()
