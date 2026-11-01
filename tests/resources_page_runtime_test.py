"""Load the real Resources page in Qt 6.8, using a counting mock backend.

Requires PySide6==6.8.3. No extracted player QML is executed.
QT_QPA_PLATFORM=offscreen python3 tests/resources_page_runtime_test.py
"""
import os
import shutil
import tempfile
import sys
import traceback
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QSG_RHI_BACKEND", "software")
from PySide6.QtCore import QObject, QUrl, QCoreApplication, QEvent
from PySide6.QtGui import QGuiApplication, QWindow
from PySide6.QtQml import QQmlComponent, QQmlEngine
from PySide6.QtTest import QTest

# GitHub job-log downloads are unavailable in this environment. Surface the
# complete assertion/QML diagnostic as check annotations as well as stderr.
if os.environ.get("GITHUB_ACTIONS"):
    def report_failure(kind, value, trace):
        for line in "".join(traceback.format_exception(kind, value, trace)).splitlines():
            escaped = line.replace("%", "%25").replace("\r", "%0D")
            print(f"::error file=tests/resources_page_runtime_test.py::{escaped}", flush=True)
        sys.__excepthook__(kind, value, trace)
    sys.excepthook = report_failure

app = QGuiApplication([])
source = Path(__file__).resolve().parents[1] / "src/qml"

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

    # Retain the components as well as their instances. Destroying a local
    # QQmlComponent can invalidate the creation context of a QML-defined mock
    # even after the instance has been assigned CppOwnership.
    components = []
    instances = []

    def create(qml):
        component = QQmlComponent(engine)
        component.setParent(engine)
        components.append(component)
        component.setData(qml.encode(), QUrl.fromLocalFile(str(Path(directory) / "test.qml")))
        obj = component.create()
        assert obj is not None, [e.toString() for e in component.errors()]
        # QML may collect an unparented mock while another component is being
        # created; the Python variable alone does not root QML-owned objects.
        QQmlEngine.setObjectOwnership(obj, QQmlEngine.ObjectOwnership.CppOwnership)
        # QWindow's parent must itself be a QWindow. Bypassing that API via
        # QObject.setParent corrupts Qt's window-parent assumptions. Retain the
        # top-level window in instances instead; only plain mocks get a parent.
        if not isinstance(obj, QWindow):
            obj.setParent(engine)
        instances.append(obj)
        return obj

    backend = create('''import QtQuick
QtObject {
    property string exePath: "player.exe"
    property bool busy: false
    property var resourceInfos: []
    property int scans: 0
    property int reads: 0
    property int budgets: 0
    signal resourcesChanged()
    function scanResources(includeBinary) { scans++; return resourceInfos.length; }
    function resourcePreviewAt(index, side) { reads++; return ""; }
    function resourcePreviewNote(index) { reads++; return ""; }
    function resourceTextAt(index) { reads++; return "import QtQuick\\nItem {}"; }
    function resourceBudgetAt(index) { budgets++; return { known: true, capacity: 1024, used: 20, bom: false }; }
    function resourcePayloadSize(index, text, bom) { return text.length; }
    function resourceTextSize(text, bom) { return text.length; }
}''')
    themes = create('''import QtQuick
QtObject { property int effectiveTheme: 0; property color accentColor: "#23e3ff" }''')
    engine.rootContext().setContextProperty("backend", backend)
    engine.rootContext().setContextProperty("themes", themes)
    window = create('''import QtQuick
import QtQuick.Window
import BSICustomizer
Window {
    visible: true; width: 1200; height: 800
    ResourcesPage { id: page; objectName: "resourcePage"; anchors.fill: parent }
    function selectFirst() { page.select(0); }
    function scan() { page.rescan(); }
}''')
    page = window.findChild(QObject, "resourcePage")
    QTest.qWait(100)
    assert backend.property("scans") == 0, "startup must not scan"
    page.setProperty("visible", False)
    page.setProperty("visible", True)
    backend.setProperty("exePath", "other-player.exe")
    QTest.qWait(100)
    assert backend.property("scans") == 0, "tab/path changes must not scan"

    rows = [dict(index=i, path=f"ui/file_{i}.qml", name=f"file_{i}.qml", folder="ui",
                 format="QML", kind="text", text=True, size=1024, stored=1024,
                 compressed=False, width=0, height=0, thumbnail="") for i in range(12000)]
    backend.setProperty("resourceInfos", rows)
    backend.resourcesChanged.emit()
    QTest.qWait(150)
    assert backend.property("reads") == 0, "delegates must not read/decode thumbnails on GUI thread"
    assert backend.property("budgets") == 0, "search/list setup must not measure selection"

    from PySide6.QtCore import QMetaObject
    QMetaObject.invokeMethod(window, "scan")
    assert backend.property("scans") == 1, "button path must scan exactly once"
    QMetaObject.invokeMethod(window, "selectFirst")
    page.setProperty("code", "my unsaved edits")
    page.setProperty("codeDirty", True)
    reads, budgets = backend.property("reads"), backend.property("budgets")
    field = window.findChild(QObject, "resourceSearchField")
    assert field is not None
    field.setProperty("text", "file_11999.qml")
    QTest.qWait(300)
    found = page.property("rows").toVariant()
    assert len(found) == 1 and found[0]["index"] == 11999, found[:2]
    assert backend.property("reads") == reads and backend.property("budgets") == budgets
    assert page.property("code") == "my unsaved edits" and page.property("codeDirty")
    assert page.property("selectedIndex") == 0, "filtering must not select a different EXE record"
    field.setProperty("text", "missing-entry")
    QTest.qWait(300)
    assert len(page.property("rows").toVariant()) == 0
    field.setProperty("text", "")
    QTest.qWait(300)
    assert len(page.property("rows").toVariant()) == 12000
    backend.setProperty("resourceInfos", [])
    backend.setProperty("exePath", "third-player.exe")
    backend.resourcesChanged.emit()
    QTest.qWait(100)
    assert page.property("selectedIndex") == -1 and page.property("code") == ""
    assert backend.property("scans") == 1, "changing target must still require manual scan"
    assert not errors, errors
    # As with the boot-provider fixtures, release Python wrappers and drain
    # Qt's DeferredDelete events before destroying composite QML type caches.
    # Never directly shiboken-delete an engine with live QML mock instances.
    window.close()
    page = field = None
    for instance in reversed(instances):
        instance.deleteLater()
    instance = None
    instances.clear()
    window = backend = themes = None
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
    print("PASS: real QML page; manual-only scans, 12,000-row search, no filter-time I/O, pending edits preserved; clean teardown")
