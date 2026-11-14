"""Numeric layout audit of the rebuilt Settings shell (harness tool, not CI).

Renders the generated QML in a real Qt Quick scene at several player sizes and
measures geometry, instead of trusting a screenshot.
"""
import os, re, tempfile, pathlib, sys
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QSG_RHI_BACKEND", "software")
from PySide6.QtCore import QObject, Property, Slot, QUrl, QTimer, QPointF
from PySide6.QtGui import QGuiApplication
from PySide6.QtQuick import QQuickWindow
from PySide6.QtQml import QQmlComponent, QQmlEngine

REPO = pathlib.Path("/home/user/BSI-Customizer")
CPP = (REPO/"src/core/SettingsPanelPatcher.cpp").read_text(encoding="utf-8")
REPL = re.search(r'QString replacement = QStringLiteral\(R"QML\((.*?)\)QML"\);', CPP, re.S).group(1)
HELPER = re.search(r'const QString helper = QStringLiteral\(R"QML\((.*?)\)QML"\);', CPP, re.S).group(1)

class P(QObject):
    def __init__(s):
        super().__init__(); s.w=1500.0; s.h=900.0; s.xv=0.0; s.yv=0.0
    @Slot()
    def startSystemMove(s): pass
    @Slot()
    def close(s): pass
    def gw(s): return s.w
    def sw(s,v): s.w=float(v)
    width=Property(float,gw,sw)
    def gh(s): return s.h
    def sh(s,v): s.h=float(v)
    height=Property(float,gh,sh)
    def gx(s): return s.xv
    def sx(s,v): s.xv=float(v)
    x=Property(float,gx,sx)
    def gy(s): return s.yv
    def sy(s,v): s.yv=float(v)
    y=Property(float,gy,sy)

PAGE = """import QtQuick 2.15
Item {
    objectName: "auditPage"
    // Stock shape: transparent root, 520px column pinned top-left,
    // footer anchored to the bottom edges.
    Item { id: col; x: 24; y: 16; width: 520; height: parent.height - 60
        Rectangle { width: parent.width; height: 30; color: "#1b1e26" } }
    Item { anchors.left: parent.left; anchors.right: parent.right
           anchors.bottom: parent.bottom; height: 46
        Rectangle { width: 100; height: 30; color: "#1b1e26" } }
    function fOnQmlLoaded() { }
}
"""

def fixture(root):
    m = root/"BsiSettingsFixture"; m.mkdir()
    (m/"qmldir").write_text("module BsiSettingsFixture\n"
        "UiShadowContainer 1.0 UiShadowContainer.qml\n"
        "UiImageButton 1.0 UiImageButton.qml\n"
        "UiSettingsButton 1.0 UiSettingsButton.qml\n"
        "singleton UiTheme 1.0 UiTheme.qml\n")
    (m/"UiShadowContainer.qml").write_text("import QtQuick 2.15\nItem { }\n")
    (m/"UiImageButton.qml").write_text('import QtQuick 2.15\nItem{width:32;height:32;property string asset:"";'
        'property int pImageWidth:16;property int pImageHeight:16;signal clicked()\n'
        'MouseArea{anchors.fill:parent;onClicked:parent.clicked()}}\n')
    (m/"UiSettingsButton.qml").write_text('import QtQuick 2.15\nRectangle{property int pSettingIdx:0;'
        'property string pSettingBtnText:"";property bool pSelected:false;property bool pShowRedDot:false;'
        'signal settingClicked(int index);radius:8;color:pSelected?"#203c55":"#182635"}\n')
    (m/"UiTheme.qml").write_text('pragma Singleton\nimport QtQuick 2.15\nQtObject{property QtObject fonts:'
        'QtObject{property font bodyLarge:Qt.font({pixelSize:17})}}\n')
    (root/"Page.qml").write_text(PAGE, encoding="utf-8")

TABS = ["Performance","Display","Graphics","Game settings","Devices","Gamepad",
        "Preferences","Phone","Shortcuts","User data","Advanced","About"]

def build(root, w, h, tabs):
    rows = ",\n".join(f'{{ pButtonText: "{t}", pShowRedDot: false, pQmlSource: "Page.qml", '
                      f'pComponentArgs: null, pComponentObj: null }}' for t in tabs)
    host = root/"Host.qml"
    host.write_text(
        'import QtQuick 2.15\nimport QtQuick.Controls 2.15\nimport QtQuick.Layouts 1.15\n'
        'import BsiSettingsFixture 1.0\nItem{\n visible:false\n'
        f' width:{w}; height:{h}\n'
        ' property var transientParent: iSettingsWindow\n property int pCurrentTabIndex: 0\n'
        ' property var pSettingsItems: ({ pSettingElements: [\n' + rows + '\n ] })\n'
        ' function fChangeSelectedSetting(index) { pCurrentTabIndex = index }\n'
        + REPL.replace("@@BSI_HIDDEN_TABS@@","[]").replace("@@BSI_TAB_ICONS@@","({})")
        + "\n" + HELPER + "\n}\n", encoding="utf-8")
    return host

def walk(item):
    out, pend = [], list(item.childItems())
    while pend:
        c = pend.pop(); out.append(c); pend.extend(c.childItems())
    return out

def audit(app, w, h, tabs=TABS, label=""):
    temp = pathlib.Path(tempfile.mkdtemp(prefix="audit-"))
    fixture(temp)
    host = build(temp, w, h, tabs)
    eng = QQmlEngine(); eng.addImportPath(str(temp))
    p = P(); p.w, p.h = float(w), float(h)
    eng.rootContext().setContextProperty("iSettingsWindow", p)
    comp = QQmlComponent(eng, QUrl.fromLocalFile(str(host)))
    obj = comp.create()
    if obj is None:
        return [("QML_ERROR", "\n".join(e.toString() for e in comp.errors()))]
    win = QQuickWindow(); win.resize(w, h)
    obj.setParentItem(win.contentItem()); win.show(); obj.setVisible(True)
    QTimer.singleShot(250, app.quit); app.exec()

    tree = walk(obj)
    errs = []
    def prog(i): return i.property("progress") if i.metaObject().className().endswith("Loader") else None
    lst = next((i for i in tree if i.metaObject().className().endswith("ListView")), None)
    stack = next((i for i in tree if i.metaObject().className().endswith("StackLayout")), None)
    page = next((i for i in tree if i.objectName() == "auditPage"), None)
    loader = next((i for i in tree if i.metaObject().className().endswith("Loader") and i.property("item") is not None), None)
    card = None
    for i in tree:
        try:
            if i.property("radius") == 12: card = i
        except RuntimeError: pass
    if any(x is None for x in (lst, stack, page, loader, card)):
        return [("MISSING", f"list={lst} stack={stack} page={page} loader={loader} card={card}")]

    fw, fh = obj.width(), obj.height()
    # --- card centering & bounds
    cx = card.mapToItem(obj, QPointF(card.width()/2, 0)).x()
    errs.append(("card centred", f"{cx:.1f} vs {fw/2:.1f}", abs(cx - fw/2) <= 1))
    gutter_l = card.mapToItem(obj, QPointF(0,0)).x()
    gutter_r = fw - (gutter_l + card.width())
    errs.append(("equal gutters", f"L={gutter_l:.1f} R={gutter_r:.1f}", abs(gutter_l-gutter_r) <= 1))
    errs.append(("card bounded", f"{card.width():.0f} of {fw:.0f}", card.width() <= min(fw-32, 568)+1))
    errs.append(("card smaller than window", f"{card.width():.0f} < {fw-100:.0f}", card.width() <= fw-100 or fw < 700))
    # --- tabs row
    tb = lst.mapToItem(obj, QPointF(0, lst.height())).y()
    ct = card.mapToItem(obj, QPointF(0,0)).y()
    errs.append(("tabs above card", f"tabs_bottom={tb:.1f} card_top={ct:.1f}", tb <= ct + 1))
    tx = lst.mapToItem(obj, QPointF(0,0)).x()
    tc = tx + lst.width()/2
    errs.append(("tabs centred", f"{tc:.1f} vs {fw/2:.1f}", abs(tc - fw/2) <= 1))
    errs.append(("tabs inside shell", f"x={tx:.1f} w={lst.width():.1f}", tx >= 0 and tx+lst.width() <= fw+0.5))
    errs.append(("tabs not clipped", f"contentW={lst.property('contentWidth')} viewW={lst.width():.1f}",
                 lst.width() >= lst.property("contentWidth")-1 or lst.width() >= fw-33))
    # horizontal scrollbar only if truly needed
    errs.append(("tabs fully visible", f"{lst.property('contentWidth')} <= {lst.width():.1f}",
                 lst.property("contentWidth") <= lst.width()+1))
    # --- page fit
    pw = page.width()
    errs.append(("page follows card", f"page={pw:.1f} card={card.width():.1f}", abs(pw - (card.width()-12)) <= 2))
    foot = next((i for i in walk(page) if abs(i.width()-page.width()) < 1 and abs(i.height()-46) < 1), None)
    errs.append(("page footer visible", "found" if foot else "missing",
                 foot is not None and foot.mapToItem(obj, QPointF(0, foot.height())).y() <= card.mapToItem(obj, QPointF(0,card.height())).y()+1))
    errs.append(("card inside shell", f"bottom={card.mapToItem(obj,QPointF(0,card.height())).y():.1f} <= {fh:.0f}",
                 card.mapToItem(obj, QPointF(0, card.height())).y() <= fh+1))
    # nothing overflows the shell width
    def clipped(i):
        n = i.parentItem()
        while n is not None and n is not obj:
            if n.metaObject().className().endswith(("ListView", "Flickable")) and n.property("clip"):
                return True
            n = n.parentItem()
        return False
    over = [i for i in tree
            if i.mapToItem(obj, QPointF(i.width(), 0)).x() > fw + 1 and i.width() > 0 and not clipped(i)]
    def chain(i):
        out=[]
        n=i
        while n is not None and n is not obj:
            out.append(n.metaObject().className()); n=n.parentItem()
        return ">".join(out[:4])
    errs.append(("no horizontal overflow", f"{len(over)} item(s) " + ",".join(
        f"{chain(i)}[x={i.mapToItem(obj,QPointF(0,0)).x():.0f} w={i.width():.0f}]" for i in over), not over))
    return errs

app = QGuiApplication([])
sizes = [(1500,900,"desktop"), (1213,680,"default"), (1024,640,"laptop"),
         (900,620,"small"), (760,560,"tiny"), (1920,1080,"fhd")]
bad = 0
for w,h,name in sizes:
    res = audit(app, w, h, label=name)
    print(f"\n== {name} {w}x{h}")
    for item in res:
        k,v,ok = item
        print(f"   [{'PASS' if ok else 'FAIL'}] {k}: {v}")
        if not ok: bad += 1
# narrow tab set
res = audit(app, 1000, 700)
print("\n== reference 1000x700")
for k,v,ok in res:
    print(f"   [{'PASS' if ok else 'FAIL'}] {k}: {v}")
    if not ok: bad += 1
print("\nFAILURES:", bad)
sys.exit(1 if bad else 0)
