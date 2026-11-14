"""Live preview of the real native UI.

Runs the actual QML application (Main.qml and every page) with Qt 6 offscreen
against explicit mock services and serves it on http://0.0.0.0:<port>. The
browser shows the live window: clicking in it delivers a real mouse click to
the running app, so the sidebar, the tab strips and the buttons all work -
including Player interface -> Settings panel. Nothing here touches an emulator,
an executable or the network.

    python .harness/preview_server.py [port]
"""
import http.server
import json
import os
import shutil
import socketserver
import sys
import queue
import tempfile
import threading
import urllib.parse
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QSG_RHI_BACKEND", "software")

from PySide6.QtCore import (Q_ARG, QBuffer, QEvent, QIODevice, QMetaObject,
                            QPointF, QTimer, QUrl)
from PySide6.QtGui import QFont, QFontDatabase, QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine, QQmlComponent, QQmlEngine
from PySide6.QtQuick import QQuickWindow
from PySide6.QtWidgets import QApplication
from PySide6.QtGui import QMouseEvent
from PySide6.QtCore import QPoint, Qt
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


class Studio:
    """Owns the offscreen app and renders frames on demand.

    Qt Quick must be driven from the thread that owns it, so HTTP requests only
    queue a (page, section) request; a timer on the GUI thread performs it and
    stores the PNG. The server therefore stays responsive while the app renders.
    """

    def __init__(self):
        self.app = QGuiApplication.instance() or QGuiApplication([])
        for name in ("Inter.ttf", "JetBrainsMono.ttf"):
            QFontDatabase.addApplicationFont(str(REPO / "src/qml/fonts" / name))
        self.app.setFont(QFont("Inter"))
        self.directory = Path(tempfile.mkdtemp(prefix="bsi-preview-"))
        module = self.directory / "BSICustomizer"
        shutil.copytree(REPO / "src/qml", module)
        (module / "qmldir").write_text("\n".join(
            ["module BSICustomizer", "singleton Theme 1.0 Theme.qml"]
            + [f"{p.stem} 1.0 {p.relative_to(module).as_posix()}"
               for p in module.rglob("*.qml") if p.name != "Theme.qml"]))
        self.engine = QQmlApplicationEngine()
        self.engine.addImportPath(str(self.directory))
        self.kept = []

        def mock(source):
            component = QQmlComponent(self.engine)
            component.setParent(self.engine)
            component.setData(source.encode(), QUrl.fromLocalFile(str(self.directory / "mock.qml")))
            obj = component.create()
            assert obj is not None, [e.toString() for e in component.errors()]
            QQmlEngine.setObjectOwnership(obj, QQmlEngine.CppOwnership)
            obj.setParent(self.engine)
            self.kept.append(obj)
            return obj

        for key, source in (("backend", BACKEND), ("auth", AUTH), ("themes", THEMES),
                            ("settings", SETTINGS), ("sysinfo", SYSINFO), ("updates", UPDATES)):
            self.engine.rootContext().setContextProperty(key, mock(source))
        self.engine.load(QUrl.fromLocalFile(str(module / "Main.qml")))
        assert self.engine.rootObjects(), "Main.qml failed to load"
        self.window = self.engine.rootObjects()[0]
        self.app.processEvents()
        # The offscreen platform reports an 800x600 screen, so the window
        # shrinks itself on load; give it a real desktop size before rendering.
        self.window.setProperty("minimumWidth", 900)
        self.window.setProperty("minimumHeight", 600)
        self.window.setProperty("width", WIDTH)
        self.window.setProperty("height", HEIGHT)
        self.app.processEvents()
        QTest.qWait(300)
        self.interface = None
        pending = [self.window.contentItem()]
        while pending:
            item = pending.pop()
            if item.objectName() == "featureInterface":
                self.interface = item
                break
            pending.extend(item.childItems())
        assert self.interface is not None, "Player interface page not found"

    def _find(self, root, name):
        pending = [root]
        while pending:
            item = pending.pop()
            if item.objectName() == name:
                return item
            pending.extend(item.childItems())
        return None

    def grab(self):
        """The window exactly as it is now."""
        return self._grab()

    def _grab(self):
        image = QQuickWindow.grabWindow(self.window)
        buffer = QBuffer()
        buffer.open(QIODevice.WriteOnly)
        image.save(buffer, "PNG")
        return bytes(buffer.data())

    def current_slug(self):
        # `currentPage` is an alias of the page stack's currentIndex; a StackLayout
        # only makes the active page visible, so this reflects real navigation.
        index = self.window.property("currentPage")
        return PAGES[index][0] if isinstance(index, int) and 0 <= index < len(PAGES) else "home"

    def click(self, x, y, right=False):
        """Deliver a real mouse click to the running window at (x, y)."""
        button = Qt.RightButton if right else Qt.LeftButton
        # QTest drives the window's own event path (hover, press, release), so
        # controls behave exactly as they do under a real cursor.
        QTest.mouseClick(self.window, button, Qt.NoModifier, QPoint(int(x), int(y)))
        self.app.processEvents()
        QTest.qWait(350)
        return self.current_slug()

    def render(self, slug, section=None):
        index = next((i for s, _l, i in PAGES if s == slug), None)
        if index is None:
            return None
        QMetaObject.invokeMethod(self.window, "navigatePage", Q_ARG("QVariant", index))
        if slug == "interface":
            bar = self._find(self.interface, "playerUiSections")
            positions = {s: i for s, _l, i in SECTIONS}
            QMetaObject.invokeMethod(bar, "setCurrentIndex",
                                     Q_ARG("int", positions.get(section or "layout", 0)))
        QTest.qWait(500)
        return self._grab()


HTML = """<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>BSI Customizer — live UI</title>
<style>
  :root { --bg:#0a0a0e; --card:#111116; --raised:#17171d; --line:#2a2a33;
          --text:#f2f2f5; --dim:#8a8a96; --accent:#7c5cff; --ready:#c8e86a; }
  * { box-sizing:border-box; }
  body { margin:0; background:var(--bg); color:var(--text); min-height:100vh;
         font:14px/1.45 Inter,system-ui,-apple-system,"Segoe UI",sans-serif; }
  header { display:flex; align-items:center; gap:14px; padding:14px 22px;
           border-bottom:1px solid var(--line); }
  .mark { width:33px; height:29px; border:1px solid var(--line); border-radius:7px;
          display:grid; place-items:center; font:600 9px/1 Inter; letter-spacing:.5px;
          color:var(--dim); }
  h1 { font-size:15px; margin:0; font-weight:600; letter-spacing:-.2px; }
  header p { margin:2px 0 0; color:var(--dim); font-size:11px; }
  .pill { margin-left:auto; display:flex; align-items:center; gap:6px;
          border:1px solid #24c8e86a; background:#0cc8e86a; color:var(--ready);
          border-radius:14px; padding:5px 11px; font:600 9px/1 Inter;
          letter-spacing:.8px; text-transform:uppercase; }
  .pill i { width:4px; height:4px; border-radius:2px; background:var(--ready); }
  main { display:grid; grid-template-columns:210px minmax(0,1fr); gap:20px;
         padding:20px; align-items:start; }
  nav { position:sticky; top:20px; display:flex; flex-direction:column; gap:2px; }
  nav .group { margin:14px 0 4px; padding-left:12px; font:600 9px/1 Inter;
        letter-spacing:1.2px; color:#5c5c68; text-transform:uppercase; }
  nav button { all:unset; cursor:pointer; padding:9px 12px; border-radius:9px;
               color:var(--dim); font-size:12.5px; }
  nav button:hover { background:#7c5cff14; color:var(--text); }
  nav button[aria-current="true"] { background:var(--raised); color:var(--text);
        font-weight:600; box-shadow:inset 2px 0 0 var(--accent); }
  .stage { border:1px solid var(--line); border-radius:14px; overflow:hidden;
           background:var(--card); position:relative; box-shadow:0 24px 60px #0008;
           max-width:1440px; }
  .stage img { display:block; width:100%; height:auto; cursor:crosshair;
               user-select:none; -webkit-user-drag:none; }
  .hint { display:flex; gap:10px; flex-wrap:wrap; align-items:center;
          color:var(--dim); font-size:12px; margin:12px 2px 0; }
  .tabs { display:flex; gap:6px; flex-wrap:wrap; margin-left:auto; }
  .tabs button { all:unset; cursor:pointer; padding:6px 11px; border-radius:8px;
                 border:1px solid var(--line); color:var(--dim); font-size:11.5px; }
  .tabs button:hover { border-color:#7c5cff88; color:var(--text); }
  .tabs button[aria-current="true"] { background:var(--raised); color:var(--text);
                 border-color:#7c5cff66; font-weight:600; }
  .note { color:#5c5c68; font-size:11.5px; margin:16px 2px 0; }
  kbd { border:1px solid var(--line); border-radius:5px; padding:1px 5px;
        font:600 10px/1.6 Inter; color:var(--dim); }
</style></head>
<body>
<header>
  <div class="mark">BSI</div>
  <div><h1>BSI Customizer</h1><p>Live app — the real Qt Quick UI, driven by your clicks</p></div>
  <div class="pill"><i></i>Live</div>
</header>
<main>
  <nav id="pages"></nav>
  <div>
    <div class="stage"><img id="shot" alt="The running application"></div>
    <div class="hint">
      <span id="caption"></span>
      <div class="tabs" id="tabs"></div>
    </div>
    <p class="note">Click anywhere in the window to drive the app — the sidebar,
      the tab strip and the buttons all respond. <kbd>alt</kbd>+click to send a
      right click. Nothing here touches an emulator, an executable or the network.</p>
  </div>
</main>
<script>
const PAGES = __PAGES__;
const SECTIONS = __SECTIONS__;
const img = document.getElementById('shot');
const caption = document.getElementById('caption');
const tabs = document.getElementById('tabs');
let current = (location.hash || '#home').slice(1);
let section = 'layout';

function label() {
  if (current === 'interface')
    return 'Player interface · ' + (SECTIONS.find(s => s.slug === section) || SECTIONS[0]).label;
  const page = PAGES.find(p => p.slug === current);
  return page ? page.label : current;
}

function paint() {
  const key = current === 'interface' ? 'interface::' + section : current;
  img.src = '/frame/' + key + '.png?t=' + Date.now();
  caption.textContent = label();
  document.querySelectorAll('#pages button').forEach(b =>
    b.setAttribute('aria-current', b.dataset.slug === current));
  tabs.innerHTML = '';
  if (current === 'interface') {
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

// The screenshot is the app: translate a click into app coordinates and ask
// the server to deliver it to the running Qt window.
img.onclick = event => {
  const box = img.getBoundingClientRect();
  const x = Math.round((event.clientX - box.left) * img.naturalWidth / box.width);
  const y = Math.round((event.clientY - box.top) * img.naturalHeight / box.height);
  const right = event.altKey;
  img.style.opacity = .6;
  fetch('/click?x=' + x + '&y=' + y + (right ? '&button=right' : ''))
    .then(r => r.text())
    .then(next => { if (next) { current = next; } img.style.opacity = 1; paint(); })
    .catch(() => { img.style.opacity = 1; });
};

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
paint();
</script>
</body></html>"""


def build_html():
    # Group by label, never by list offset: an off-by-one here silently drops a
    # whole page from the gallery (Player interface went missing exactly that
    # way, taking its Settings-panel tab with it).
    groups = [
        ("HOME", ["home"]),
        ("PATCHER", ["theme", "resources", "fps", "integrity", "boot", "interface"]),
        ("PRESENCE", ["rpcid", "gif"]),
        ("TOOLS", ["workspace", "debloat"]),
        ("RECOVERY", ["backups"]),
        ("COLLECTION", ["addons"]),
    ]
    labels = {slug: label for slug, label, _index in PAGES}
    pages = []
    for group, slugs in groups:
        pages.append({"group": group})
        for slug in slugs:
            pages.append({"slug": slug, "label": labels[slug]})
    missing = set(labels) - {entry["slug"] for entry in pages if "slug" in entry}
    assert not missing, f"pages missing from the preview gallery: {sorted(missing)}"
    return (HTML.replace("__PAGES__", json.dumps(pages))
                .replace("__SECTIONS__", json.dumps(
                    [{"slug": s, "label": l} for s, l, _i in SECTIONS])))


def main():
    """Serve the live app.

    Qt Quick belongs to the thread that created it. The main thread therefore
    owns the application AND runs the Qt event loop; the HTTP server lives in a
    background thread, drops jobs on a queue and waits for the GUI thread to run
    them. Rendering from an HTTP thread silently returns stale frames.
    """
    studio = Studio()
    jobs = queue.Queue()
    html = build_html().encode()

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            parsed = urllib.parse.urlparse(self.path)
            path = parsed.path.rstrip("/") or "/"
            try:
                if path in ("/", "/index.html"):
                    body, kind = html, "text/html; charset=utf-8"
                elif path == "/click":
                    query = urllib.parse.parse_qs(parsed.query)
                    x = float(query["x"][0])
                    y = float(query["y"][0])
                    right = query.get("button", [""])[0] == "right"
                    body = self.run_on_qt(lambda: studio.click(x, y, right)).encode()
                    kind = "text/plain; charset=utf-8"
                elif path.startswith("/frame/"):
                    key = path[len("/frame/"):]
                    if key.endswith(".png"):
                        key = key[:-len(".png")]
                    if key == "current":
                        # What the app shows right now - used after a click.
                        body = self.run_on_qt(studio.grab)
                        kind = "image/png"
                        if body is None:
                            self.send_error(503, "nothing rendered yet")
                            return
                        self.send_response(200)
                        self.send_header("Content-Type", kind)
                        self.send_header("Content-Length", str(len(body)))
                        self.send_header("Cache-Control", "no-store")
                        self.end_headers()
                        self.wfile.write(body)
                        return
                    slug, _, section = key.partition("::")
                    body = self.run_on_qt(lambda: studio.render(slug, section or None))
                    if body is None:
                        self.send_error(404, "unknown page")
                        return
                    kind = "image/png"
                else:
                    self.send_error(404)
                    return
            except TimeoutError:
                self.send_error(504, "render timeout")
                return
            except Exception as error:  # report, never kill the server
                self.send_error(500, str(error))
                return
            self.send_response(200)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        @staticmethod
        def run_on_qt(fn, timeout=30):
            box, done = {}, threading.Event()
            jobs.put((fn, box, done))
            if not done.wait(timeout):
                raise TimeoutError("the UI did not finish the request")
            if "error" in box:
                raise box["error"]
            return box.get("value")

        def log_message(self, *_args):
            pass

    def pump():
        while True:
            try:
                fn, box, done = jobs.get_nowait()
            except queue.Empty:
                return
            try:
                box["value"] = fn()
            except Exception as error:
                box["error"] = error
            finally:
                done.set()

    timer = QTimer()
    timer.timeout.connect(pump)
    timer.start(15)

    socketserver.ThreadingTCPServer.allow_reuse_address = True
    socketserver.ThreadingTCPServer.daemon_threads = True
    httpd = socketserver.ThreadingTCPServer(("0.0.0.0", PORT), Handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    print(f"BSI UI preview on http://0.0.0.0:{PORT} — open the app, click through it",
          flush=True)
    # Keep the GUI thread's event loop running; it does the rendering.
    studio.app.exec()


if __name__ == "__main__":
    main()
