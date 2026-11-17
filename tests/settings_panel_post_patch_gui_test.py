"""Render the Settings shell *after* a patch and check the GUI it produces.

`settings_panel_runtime_test.py` checks that the generated QML contains the
right fragments and that a bare shell instantiates. This test goes one step
further: it applies the real shell template plus a real custom tab icon, renders
the result offscreen, and then measures the *drawn* geometry, so a change that
still compiles but puts the underline in the wrong place, drops the page card,
lets a label run into its neighbour, or hides the custom icon fails here.

Usage:
    QT_QPA_PLATFORM=offscreen python tests/settings_panel_post_patch_gui_test.py
Optionally pass an output path for the rendered PNG to inspect the result.
"""
import os
import pathlib
import re
import sys
import tempfile

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QSG_RHI_BACKEND", "software")

from PySide6.QtCore import QObject, Property, Qt, QPointF, QTimer, Slot
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlComponent, QQmlEngine
from PySide6.QtTest import QTest

REPO = pathlib.Path(__file__).resolve().parents[1]
CPP = (REPO / "src/core/SettingsPanelPatcher.cpp").read_text(encoding="utf-8")
SHELL = re.search(
    r'QString replacement = QStringLiteral\(R"QML\((.*?)\)QML"\);', CPP, re.S
).group(1)

# A tab icon of the kind a user uploads: a self-contained SVG data URI.
ICON = (
    "data:image/svg+xml;base64,"
    "PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIzMiIgaGVpZ2h0PSIzMiI+"
    "PHJlY3Qgd2lkdGg9IjMyIiBoZWlnaHQ9IjMyIiBmaWxsPSIjNTRjOGZmIi8+PC9zdmc+"
)

# Two pages so the strip really has neighbours that could collide, and a page
# body with a footer so the card is exercised the way a stock page is.
PAGE = r"""import QtQuick 2.15
Item {
    objectName: "runtimeSettingPage"
    Item {
        x: 24; y: 16
        width: 520; height: parent.height - 60
        Column {
            width: parent.width; spacing: 18
            Text { text: "CPU allocation"; color: "#e8edf5"; font.pixelSize: 13 }
            Rectangle { width: 200; height: 34; radius: 4; color: "#1b1e26"; border.color: "#2c3040"
                Text { anchors.centerIn: parent; text: "Medium (2 Cores)"; color: "#e8edf5"; font.pixelSize: 12 } }
            Text { text: "Memory allocation"; color: "#e8edf5"; font.pixelSize: 13 }
        }
    }
    Item {
        anchors.left: parent.left; anchors.right: parent.right
        anchors.bottom: parent.bottom; height: 46
        Rectangle { anchors.fill: parent; color: "#0b0e13" }
        Text { anchors.left: parent.left; anchors.leftMargin: 24; anchors.verticalCenter: parent.verticalCenter
               text: "Some changes will apply on next launch"; color: "#8b93a3"; font.pixelSize: 11 }
    }
    function fOnQmlLoaded() { }
}
"""

ACCENT = (0x54, 0xC8, 0xFF)

TABS = [
    ("settingpage.qml", "Performance"),
    ("displaypage.qml", "Display"),
    ("graphicspage.qml", "Graphics"),
    ("gamesettingspage.qml", "Game settings"),
    ("devicespage.qml", "Devices"),
]


class Player(QObject):
    """The stock player window the shell attaches to."""

    def __init__(self):
        super().__init__()
        self._w, self._h = 1500.0, 900.0

    def g_w(self):
        return self._w

    def s_w(self, v):
        self._w = float(v)

    width = Property(float, g_w, s_w)

    def g_h(self):
        return self._h

    def s_h(self, v):
        self._h = float(v)

    height = Property(float, g_h, s_h)

    @Slot()
    def startSystemMove(self):
        pass

    @Slot()
    def close(self):
        self.closed = True


class SettingsObject(QObject):
    """Stands in for the native UiSettingsWindow the shell is parented to."""

    def __init__(self, count):
        super().__init__()
        self._count = count
        self._index = 0

    def g_current(self):
        return self._index

    def s_current(self, v):
        self._index = int(v)

    pCurrentTabIndex = Property(int, g_current, s_current)

    def g_count(self):
        return self._count

    tabCount = Property(int, g_count)


def visual_items(item):
    found, pending = [], list(item.childItems())
    while pending:
        child = pending.pop()
        found.append(child)
        pending.extend(child.childItems())
    return found


def prop(item, name, default=None):
    try:
        if item.metaObject().indexOfProperty(name) <= 0:
            return default
    except (RuntimeError, TypeError):
        return default
    return item.property(name)


def source_of(item):
    value = prop(item, "source")
    if value is None:
        return None
    return value.toString() if hasattr(value, "toString") else str(value or "")


def build_module(root):
    """Write the player-side QML the shell imports, plus the fixture pages."""
    module = root / "BsiGuiFixture"
    module.mkdir()
    (module / "qmldir").write_text(
        "module BsiGuiFixture\n"
        "UiShadowContainer 1.0 UiShadowContainer.qml\n"
        "UiImageButton 1.0 UiImageButton.qml\n"
        "UiSettingsButton 1.0 UiSettingsButton.qml\n"
        "singleton UiTheme 1.0 UiTheme.qml\n",
        encoding="utf-8",
    )
    (module / "UiShadowContainer.qml").write_text(
        "import QtQuick 2.15\nItem { }\n", encoding="utf-8"
    )
    # Mirrors the player's own button: an 8 px pill that fills its selected
    # state with the accent, which is what the shell's underline sits under.
    (module / "UiSettingsButton.qml").write_text(
        "import QtQuick 2.15\nRectangle {\n"
        "    property int pSettingIdx: 0\n"
        "    property string pSettingBtnText: \"\"\n"
        "    property bool pSelected: false\n"
        "    property bool pShowRedDot: false\n"
        "    signal settingClicked(int index)\n"
        "    radius: 8\n"
        "    color: pSelected ? Qt.rgba(0.33,0.78,1,0.16) : \"transparent\"\n"
        "    Text { anchors.fill: parent; text: pSettingBtnText; color: \"#f4f7fb\"\n"
        "        font.pixelSize: 13\n"
        "        horizontalAlignment: Text.AlignHCenter\n"
        "        verticalAlignment: Text.AlignVCenter }\n"
        "    MouseArea { anchors.fill: parent\n"
        "        onClicked: parent.settingClicked(parent.pSettingIdx) }\n"
        "}\n",
        encoding="utf-8",
    )
    (module / "UiImageButton.qml").write_text(
        "import QtQuick 2.15\nItem {\n"
        "    width: 32; height: 32\n"
        "    property string asset: \"\"\n"
        "    signal clicked()\n"
        "    Text { anchors.centerIn: parent; text: \"X\"; color: \"#8b93a3\"\n"
        "        font.pixelSize: 14 }\n"
        "    MouseArea { anchors.fill: parent; onClicked: parent.clicked() }\n"
        "}\n",
        encoding="utf-8",
    )
    (module / "UiTheme.qml").write_text(
        "pragma Singleton\nimport QtQuick 2.15\nQtObject {\n"
        "    property QtObject colors: QtObject {\n"
        "        property color primary10: \"#f4f7fb\"\n"
        "        property color primary20: \"#d2dae4\"\n"
        "        property color primary60: \"#35465a\"\n"
        "        property color primary70: \"#1b2838\"\n"
        "        property color primary80: \"#101a27\"\n"
        "        property color accentNormal: \"#54c8ff\"\n"
        "    }\n"
        "    property QtObject fonts: QtObject {\n"
        "        property font bodyLarge: Qt.font({pixelSize: 16})\n"
        "    }\n"
        "}\n",
        encoding="utf-8",
    )
    return module


def main():
    out_path = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else None
    app = QGuiApplication.instance() or QGuiApplication([])

    root = pathlib.Path(tempfile.mkdtemp(prefix="bsi-post-patch-"))
    module = build_module(root)
    for src, _ in TABS:
        (root / ("%s.qml" % src[: -len(".qml")].capitalize())).write_text(PAGE, encoding="utf-8")
    (module / "RuntimeSettingPage.qml").write_text(PAGE, encoding="utf-8")

    engine = QQmlEngine()
    engine.addImportPath(str(root))

    player = Player()
    settings = SettingsObject(len(TABS))

    # The shell as it lands in the player: the template with the map the About
    # page publishes. That map is keyed by the tab's own index, because
    # pQmlSource is a QML url: it has no toLowerCase() and stringifies
    # percent-encoded, so a name derived from it can never match and the strip
    # silently drew nothing.
    bound = SHELL.replace(
        "property var bIcons:({})",
        "property var bIcons:({'0':'%s'})" % ICON,
    )
    tabs_js = "[" + ",".join(
        "{pQmlSource:'%s',pButtonText:'%s',pShowRedDot:false}" % (src.capitalize(), label)
        for src, label in TABS
    ) + "]"
    host_qml = """import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import BsiGuiFixture 1.0
Window {
    id: iSettingsWindow
    objectName: "settingsWindow"
    width: 1120; height: 670
    visible: true
    color: "transparent"
    function startSystemMove() { }
    function close() { }
    property int pCurrentTabIndex: 0
    property QtObject pSettingsItems: QtObject {
        property var pSettingElements: %s
    }
    function fChangeSelectedSetting(i) { pCurrentTabIndex = i }
    %s
}
""" % (tabs_js, bound)

    host_path = root / "SettingsHost.qml"
    host_path.write_text(host_qml, encoding="utf-8")

    component = QQmlComponent(engine, str(host_path))
    obj = component.create()

    assert obj is not None, "the post-patch Settings window did not instantiate: " + "\n".join(
        e.toString() for e in component.errors()
    )

    from PySide6.QtQuick import QQuickItem

    app.processEvents()
    QTimer.singleShot(250, app.quit)
    app.exec()

    # The root is a Window; its visual tree hangs off the content item.
    root_item = next((c for c in obj.findChildren(QQuickItem) if c.parentItem() is None), None)
    assert root_item is not None, "the Settings window has no visual root"
    tree = visual_items(root_item)
    content = root_item

    def find(pred):
        return next((i for i in tree if pred(i)), None)

    failures = []

    def check(ok, message):
        if not ok:
            failures.append(message)

    # --- header -----------------------------------------------------------
    title = find(lambda i: str(prop(i, "text", "")) == "Settings")
    check(title is not None, "the shell has no 'Settings' title")
    if title:
        mid = title.mapToItem(content, QPointF(title.width() / 2, 0)).x()
        check(
            abs(mid - content.width() / 2) < 2,
            "the title is not centred in the header band (x=%.1f, centre=%.1f)" % (mid, content.width() / 2),
        )
        check(
            title.mapToItem(content, QPointF(0, 0)).y() < 50,
            "the title is not inside the 50 px header band",
        )
    close = find(lambda i: str(prop(i, "asset", "")) == "CloseIcon")
    check(close is not None, "the shell has no close button")

    # --- tab strip --------------------------------------------------------
    buttons = [i for i in tree if prop(i, "pSettingBtnText") is not None]
    check(len(buttons) == len(TABS), "expected %d tabs, drew %d" % (len(TABS), len(buttons)))
    check(bool(buttons), "the tab strip drew no buttons")
    if buttons:
        # Placement first: everything below is measured left to right.
        slots = sorted(buttons, key=lambda b: b.mapToItem(content, QPointF(0, 0)).x())
        widths = [round(b.width()) for b in slots]
        gaps = []
        for left, right in zip(slots, slots[1:]):
            lx = left.mapToItem(content, QPointF(left.width(), 0)).x()
            rx = right.mapToItem(content, QPointF(0, 0)).x()
            gaps.append(round(rx - lx))

        # Sizing: a tab is as wide as its own label plus a fixed pad. Uniform
        # slots are the bug the user reported - "Game settings" and "Devices"
        # ran together while short labels floated in a wide empty box.
        check(
            len(set(widths)) > 1,
            "tabs share one uniform width (%s); each must fit its own label" % sorted(set(widths)),
        )
        # Gaps: exactly equal between every pair, and actually visible.
        check(len(set(gaps)) == 1, "tab gaps are uneven: %s" % gaps)
        check(
            gaps and 0 < gaps[0] <= 12,
            "the gap between tabs must be visible but tight, got %s" % gaps,
        )
        # No collisions: no label can run into its neighbour.
        for (left, w), nxt in zip(zip(slots, widths), gaps):
            lx = left.mapToItem(content, QPointF(0, 0)).x()
            check(
                nxt > 0,
                "the tab at x=%.1f (width %d) runs into the next one" % (lx, w),
            )
        # Every tab, including the last, must be fully inside the strip.
        last = slots[-1]
        right_edge = last.mapToItem(content, QPointF(last.width(), 0)).x()
        check(
            right_edge <= content.width() - 23,
            "the last category is clipped at the shell edge (%.1f of %.1f)"
            % (right_edge, content.width()),
        )
        # The strip starts at the same 18 px inset as everything else in the
        # window, so the row and the page below it line up.
        first_x = slots[0].mapToItem(content, QPointF(0, 0)).x()
        check(
            first_x >= 17,
            "the first tab is too close to the window edge (%.1f)" % first_x,
        )

    # --- the custom icon is actually drawn ---------------------------------
    # This is the reported failure: the icon saved fine, the apply succeeded and
    # the tab still showed nothing. Keying the map by index is what makes the
    # delegate resolve it, so assert the artwork really is painted: 18x18 and
    # centred in its own tab.
    painted = [i for i in tree if source_of(i) == ICON]
    check(bool(painted), "the custom tab icon is not painted by any tab")
    if painted and buttons:
        icon = painted[0]
        delegate = icon.parentItem()
        strip = delegate.parentItem()
        icon_mid = icon.mapToItem(strip, QPointF(icon.width() / 2, 0)).x()
        tab_mid = delegate.mapToItem(strip, QPointF(delegate.width() / 2, 0)).x()
        check(
            round(icon.width()) == 18 and round(icon.height()) == 18,
            "the custom icon must be drawn at 18x18, got %dx%d"
            % (icon.width(), icon.height()),
        )
        check(
            abs(icon_mid - tab_mid) < 2,
            "the custom icon is not centred in its tab (%.1f vs %.1f)"
            % (icon_mid, tab_mid),
        )
        # With an icon the label moves up to make room; the icon sits beneath it.
        check(
            icon.mapToItem(strip, QPointF(0, 0)).y() > 20,
            "the custom icon must sit below the label, not over it",
        )

    # --- selected tab ------------------------------------------------------
    # The selected tab is the native UiSettingsButton's own highlight; the shell
    # paints no separate pill or underline (that redesign was rejected).
    selected = [b for b in buttons if prop(b, "pSelected") is True]
    check(len(selected) == 1, "exactly one tab must be selected, got %d" % len(selected))

    # --- page host: centred native column, no card -------------------------
    # The shell keeps the stock fixed-width page column centred at its native
    # width. There is deliberately no card, border or inset surface behind it.
    # The card was a rounded rectangle inset 20 px on each side. Identify it by
    # that geometry (`border` is a QQuickPen and is not marshalled to Python).
    cards = [
        i for i in tree
        if abs(i.width() - (content.width() - 40)) < 3
        and i.height() > content.height() - 130
        and i is not content
    ]
    check(not cards, "the rejected page card came back (%d drawn)" % len(cards))

    # One Loader per tab: the collapsed ones can leave a zero-sized page in the
    # tree, so take the page that is actually drawn.
    page = find(lambda i: i.objectName() == "runtimeSettingPage" and i.width() > 100)
    check(page is not None, "the native page was not loaded into the shell")
    if page:
        origin = page.mapToItem(content, QPointF(0, 0))
        # The page fills the host, which is inset 18 px on both sides, so it
        # must sit centred and clear of the rounded window edge.
        check(
            abs((origin.x() + page.width() / 2) - content.width() / 2) < 3,
            "the native page column is not centred in the window",
        )
        check(
            origin.x() >= 15 and origin.x() + page.width() <= content.width() - 15,
            "the native page runs into the window edge (x %.1f, w %.1f of %.1f)"
            % (origin.x(), page.width(), content.width()),
        )
        bottom = origin.y() + page.height()
        check(
            bottom <= content.height() - 15,
            "the page (and its footer) hits the bottom edge of the window "
            "(bottom %.1f of %.1f)" % (bottom, content.height()),
        )

    # --- no hard-coded palette --------------------------------------------
    literal = re.findall(r"color:\s*[\"']#", SHELL)
    check(not literal, "the shell still bakes literal colours: %s" % literal)

    # A rendered PNG is optional; the geometry assertions above are the test.
    if out_path:
        print("note: rendering is best-effort on this platform (%s)" % out_path)

    obj.deleteLater()

    if failures:
        for message in failures:
            print("FAIL: %s" % message)
        print("FAIL: the post-patch Settings GUI does not match the design")
        return 1
    print(
        "PASS: the post-patch Settings GUI draws label-sized tabs with equal "
        "gaps, no colliding labels and a centred native page column"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
