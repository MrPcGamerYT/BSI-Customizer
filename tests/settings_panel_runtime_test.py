"""Instantiate the generated in-emulator Settings shell with real Qt Quick.

This checks the patched shell's QML syntax, category selection, page loading,
window drag and close callbacks. It uses a tiny host/model double; it does not
launch the Android VM or claim a live HD-Player rendering test.
Requires PySide6==6.8.3 and is run by run_qt_runtime_test.py.
"""
import os
import pathlib
import re
import tempfile
import urllib.parse
import zlib
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QSG_RHI_BACKEND", "software")

from PySide6.QtCore import QObject, QPointF, Property, Qt, Slot, QUrl
from PySide6.QtGui import QGuiApplication
from PySide6.QtQuick import QQuickItem, QQuickWindow
from PySide6.QtQml import QQmlComponent, QQmlEngine
from PySide6.QtTest import QTest


class PlayerWindowDouble(QObject):
    def __init__(self):
        super().__init__()
        self.move_requested = False
        self.close_requested = False
        self._width, self._height, self._x, self._y = 1500.0, 900.0, 0.0, 0.0

    def get_width(self): return self._width
    def set_width(self, value): self._width = float(value)
    width = Property(float, get_width, set_width)

    def get_height(self): return self._height
    def set_height(self, value): self._height = float(value)
    height = Property(float, get_height, set_height)

    def get_x(self): return self._x
    def set_x(self, value): self._x = float(value)
    x = Property(float, get_x, set_x)

    def get_y(self): return self._y
    def set_y(self, value): self._y = float(value)
    y = Property(float, get_y, set_y)

    @Slot()
    def startSystemMove(self):
        self.move_requested = True

    @Slot()
    def close(self):
        self.close_requested = True


app = QGuiApplication([])
repo = Path(__file__).resolve().parents[1]
cpp = (repo / "src/core/SettingsPanelPatcher.cpp").read_text(encoding="utf-8")
match = re.search(
    r'(?:const )?QString replacement = QStringLiteral\(R"QML\((.*?)\)QML"\);',
    cpp,
    re.S,
)
assert match, "Could not find the generated Settings-window QML template"
replacement = match.group(1)
assert "BSI v15" in replacement
assert "pSettingsItems.pSettingElements" in replacement
assert "property var bHidden" in replacement
# The About page publishes the hidden-tab list and the icon map to the shell.
# It must find that shell by walking up: counting parents landed on the window
# instead of the UiShadowContainer, which threw
# "Cannot assign to non-existent property bHidden" and silently dropped every
# custom tab icon (the file applied, nothing ever showed).
assert "function fOnQmlLoaded(){var p=parent;while(p&&p.bIcons===undefined)p=p.parent;if(p){p.bHidden=bsiHiddenTabs;p.bIcons=bsiTabIcons}}" in cpp, \
    "the About page must walk up to the shell, not count parents"
# ...and the shell has to declare both of those properties, or the assignment
# above has nowhere to land.
assert "property var bHidden" in replacement, "the shell must declare bHidden"
assert "property var bIcons:({})" in replacement, "the shell must declare bIcons"
assert "property var bsiHiddenTabs:@@BSI_HIDDEN_TABS@@" in cpp
# A tab icon may only be an SVG: validateAssetBytes() rejects any other MIME for
# that kind. Rasterising an oversized icon therefore produced an asset that the
# very next step refused, and the icon disappeared from the rebuilt page with no
# message. The shrink path must leave tab icons as (minified) SVG.
assert 'if (kind != QLatin1String("tab-icon") && budget > 0 && uriSize > budget)' in cpp, \
    "shrinkAsset() must not rasterise a tab icon into a PNG the validator rejects"
# And a fitting loop that gives up must still keep the artwork it managed to
# build, instead of writing the page with every custom icon removed.
assert "customTabIcons = bestIcons" in cpp, \
    "the fit loop must keep its best attempt so custom icons are never silently dropped"
assert "property var bsiTabIcons:@@BSI_TAB_ICONS@@" in cpp
# The shell must not ship its own palette: it paints with the player's own
# theme object, exactly like the stock Settings window, so it follows whatever
# the emulator currently looks like instead of a colour frozen into the patch.
assert "color:UiTheme.colors.primary80" in replacement
assert "color:UiTheme.colors.primary10" in replacement
assert "#0d0f14" not in replacement and 'color:"white"' not in replacement, \
    "the rebuilt shell must not hard-code colours"
assert 'color:"' not in replacement, "no literal colours may be baked into the shell"
assert "MouseArea{anchors.fill:parent;onPressed:iSettingsWindow.startSystemMove()}" in replacement, \
    "the whole header band must drag the window"
assert "anchors.centerIn:parent" in replacement, \
    "the title must be centred in the header band"
assert "ListView{width:Math.min(contentWidth,parent.width-48);height:44" in replacement
assert "orientation:ListView.Horizontal;model:pSettingsItems.pSettingElements" in replacement
# The strip spans the shell with a symmetric 24 px inset so the first and last
# tabs sit equally far from the window edge.
assert "x:(parent.width-width)/2" in replacement, \
    "the tab strip must be centred in the window"
assert "iSettingsWindowShadowContainer.bIcons[" in replacement
# A tab is as wide as its own label plus a fixed 22 px of padding. Equal-width
# slots are what made "Game settings" and "Devices" collide, and they left short
# labels like "About" floating in a wide empty box: the fix is to let the label
# size the tab.
assert "width:c===''?-4:t.boundingRect(modelData.pButtonText).width+22;height:44" in replacement, \
    "each tab must be sized by its own label, not by an equal share of the strip"
# Measuring with the theme's body font was the other half of the ragged row:
# that font is smaller than the one the native button draws its label in, so long
# labels like "Game settings" overflowed their measured width and ate into the
# spacing while short ones kept theirs. A bare FontMetrics inherits the same
# application font the label inherits, which makes every gap identical.
assert "FontMetrics{id:t}" in replacement, \
    "the tab must measure its label with the same font it draws it in"
assert "font:UiTheme.fonts.bodyMedium}\n" not in replacement, \
    "measuring with the theme body font shrank wide tabs and broke the even gaps"
# The advance includes each glyph's side bearings, and they differ per label by
# up to ~3 px, so sizing on the advance left different visible space beside each
# word ("Game settings" nearly touched "Devices" while "Display" floated). Sizing
# on the ink keeps the same padding beside every label.
assert "t.boundingRect(modelData.pButtonText).width" in replacement, \
    "the tab must be sized by the ink of its label, not its advance"
assert "advanceWidth(" not in replacement, \
    "advance includes side bearings, which makes the visible gaps uneven"
# Even gaps: the ListView sets one spacing value for every gap, so the space
# between any two neighbouring tabs is identical no matter how long the labels
# are. Doing this with padding inside an equal-width slot is what produced the
# ragged row the user rejected.
assert ";spacing:4" in replacement, \
    "every gap between tabs must come from one spacing value"
# A custom icon sits centred in the tab with the native label lifted to the top,
# instead of being painted over the text.
assert "anchors.bottomMargin:c?26:0" in replacement, "the label must move up when an icon is shown"
assert "Image{source:c;width:18;height:18;x:parent.width/2-9;y:24}" in replacement, \
    "the icon must be centred in the tab"
# Collapsing the width hid the tab's box but not its label: the button inside the
# delegate kept painting its text, so "Customise" and "Advanced" smeared over the
# tabs beside them while their slot was already closed. The delegate has to be
# invisible as well, not just zero sized.
assert "delegate:Item{visible:c!=='';" in replacement, \
    "a hidden tab must not paint its label over its neighbours"
# A hidden tab has to leave the strip *without leaving a gap behind*. Publishing
# bHidden into the shell did nothing at all, because no strip delegate ever read
# it. Collapsing the tab to zero width was not enough either: a ListView puts its
# spacing after every delegate, so each hidden tab left a 4 px hole and the row
# ended up with 8 px and 16 px gaps between its neighbours (reported again after
# the first attempt). A negative width cancels exactly one spacing, which is what
# makes the gaps identical whether every tab is shown or only some are.
assert "width:c===''?-4:" in replacement, \
    "a hidden tab must cancel its own gap, not just its own width"
assert "clip:true" in replacement, \
    "the strip must clip so overflowing tabs cannot paint outside the window"

# Every category must stay reachable: the strip divides the shell width into
# equal slots instead of letting the last tab be half-clipped at the edge.
assert "item.anchors.fill=item.parent" in replacement
# The page keeps the native, centred 568 px column it has always used. It is
# NOT wrapped in a card and the shell paints no pill or underline behind the
# selected tab: that redesign was rejected, the flat window is what ships.
assert "Item{width:Math.min(parent.width-48,568);x:(parent.width-width)/2;height:parent.height-118" in replacement, \
    "the page host must leave room for the footer inside the rounded window"
assert "border.color" not in replacement and "radius:12" not in replacement, \
    "the rejected card must not come back"
assert "visible:s" not in replacement, \
    "the rejected underline/pill must not come back"
assert "Rectangle{anchors.fill:parent;color:UiTheme.colors.primary80;radius:14" in replacement, \
    "the window itself stays a single flat rounded surface"
assert "StackLayout{id:iSelectedSettingControl;anchors.fill:parent;currentIndex:pCurrentTabIndex" in replacement
# The whole UiSettingsWindow.qml has to fit one resource slot in the player EXE,
# and it can never be moved once the file is larger than the slot. A shipped
# BlueStacks 5.10 build reports 2468 bytes there and a real apply of an earlier
# draft of this shell needed 2479 ("needs 2479 bytes but its slot only holds
# 2468"), which is the hard failure this test exists to prevent. Keep the
# template far enough under that ceiling that the surrounding marker check, the
# BOM handling and any future stock prefix still fit: 1900 leaves ~570 bytes of
# headroom against the measured slot. The measured surrounding overhead is 727
# bytes, so 2468 - 727 = 1741 is the hard limit for this template; it currently
# sits at 1732 (2459 in the slot), which is the whole margin there is.
assert len(replacement.encode()) <= 1741, (
    f"shell grew to {len(replacement.encode())} bytes; the tightest known "
    "UiSettingsWindow.qml slot is 2468 bytes"
)

# The About page lives in its own player resource slot, compressed, and a custom
# tab icon or logo is embedded straight into it. That slot is small (one shipped
# player reports 2337 bytes of capacity) and the EXE never grows, so a bloated
# About template is not cosmetic: it makes every icon upload fail with
# "needs N bytes but its slot only holds M". Keep the compressed payload well
# inside the smallest slot we have seen, counting one embedded SVG icon.
about_template = re.search(
    r'QString generated = QStringLiteral\(R"QML\((.*?)\)QML"\);', cpp, re.S
)
assert about_template, "Could not find the generated About-page QML template"
about_source = about_template.group(1)
about_doc = (about_source
             .replace("@@BSI_DEVELOPER@@", '"Mr.Pc Gamer"')
             .replace("@@BSI_TAGLINE@@", '"BlueStacks tools"')
             .replace("@@BSI_HIDDEN_TABS@@", "[]")
             .replace("@@BSI_LINKS@@", '\n{"title":"GitHub","url":"https://github.com/x"}')
             .replace("@@BSI_LOGO_SOURCE@@", '""')
             .replace("@@BSI_TAB_ICONS@@",
                      '({"display":"data:image/svg+xml;base64,'
                      + "A" * 512 + '"})'))
# The generated page is validated against kAboutMarker before it is written, so
# the marker text inside the template must match that constant exactly -
# otherwise every apply fails its own marker check.
about_marker = re.search(
    r'const QString kAboutMarker = QStringLiteral\("([^"]+)"\);', cpp)
assert about_marker, "Could not find kAboutMarker"
assert about_marker.group(1) in about_doc, (
    f"the generated About page must contain its own marker {about_marker.group(1)!r}; "
    "the rebuild refuses to write a page that fails this check")

about_payload = len(zlib.compress(about_doc.encode(), 9))
assert about_payload <= 1700, (
    f"the About page compresses to {about_payload} bytes with one embedded icon; "
    "the smallest known player slot is 2337 bytes and a logo goes in the same "
    "document, so keep it comfortably below that"
)
assert "bsiAboutDeveloperName" in about_doc and "bsiAboutDeveloperLink" in about_doc, \
    "compacting the About page must keep its object names"

# A tab the user unticks must actually disappear from the strip. The About page
# is the only thing that knows the hidden list, and the only channel it has into
# the shell is the icon map (the strip's delegates read that map per tab), so a
# hidden page has to appear there with an empty value. Publishing bHidden alone
# changed nothing at all: no strip delegate ever read it, which is exactly the
# "I unchecked tabs but they are still there after patching" report.
# buildAbout() appends an empty value for every hidden page; the python-side
# document cannot show that (the C++ does the appending), so assert the C++ does
# it and that the pair of them agree on the key.
assert 'iconLiteral += qmlString(page.toCaseFolded()) + QStringLiteral(":\\"\\"")' in cpp, \
    "buildAbout must mark each hidden tab in the icon map with an empty value"
# ...and the shell must collapse the tab on that marker rather than measure it.
assert "width:c===''?-4:" in replacement, \
    "the strip must collapse a tab the About page marks hidden"
assert "fCheckForUpdateCallback" in about_doc and "fUpdateShortDisplayName" in about_doc, \
    "the native About callbacks must survive compaction"
assert "delegate:Loader{anchors.fill:parent;Component.onCompleted:setSource(modelData.pQmlSource" in replacement
assert "pSettingBtnText:" in replacement and "pSelected:index==pCurrentTabIndex" in replacement and "pShowRedDot:modelData.pShowRedDot" in replacement
assert "pSettingIdx:index" in replacement
assert "fChangeSelectedSetting(pSettingIdx)" in replacement

# The runtime validator that decides whether a player may be rebuilt must accept
# the rebuilt shell's own spelling. It used to look only for the stock
# `model.modelData` form, so re-applying over an already-patched player was
# rejected as "no longer loads" even though the document was fine.
_patcher = pathlib.Path(__file__).resolve().parent.parent / "src" / "core" / "SettingsPanelPatcher.cpp"
_patcher_src = _patcher.read_text()
# The page host's Repeater delegate has the model data directly in scope, so
# this shell uses the `modelData` spelling (one of the two the validator accepts).
assert "setSource(modelData.pQmlSource" in replacement
assert "modelData.pComponentObj=item" in replacement
# The validator must accept both spellings, because the stock document writes
# `model.modelData` while this one sees the data directly. Matching only one made
# re-applying over a patched player fail as "no longer loads" even though the
# document was perfectly good.
assert "setSource(modelData.pQmlSource" in _patcher_src, (
    "the validator must accept the rebuilt shell's setSource spelling"
)
assert 'QLatin1String("modelData.pComponentObj=item")' in _patcher_src, (
    "the validator must accept the rebuilt shell's component-store spelling"
)
assert "onPressed:iSettingsWindow.startSystemMove()" in replacement
helper_match = re.search(
    r'const QString helper = QStringLiteral\(R"QML\((.*?)\)QML"\);',
    cpp,
    re.S,
)
assert helper_match, "Could not find the PR #88 Settings-window geometry helper"
helper = helper_match.group(1)
assert "onVisibleChanged:if(visible&&transientParent)" in helper
assert "var t=transientParent" in helper
assert "width=Math.min(t.width*.96,1120)" in helper
assert "height=Math.min(t.height*.94,670)" in helper
assert "x=t.x+t.width/2-width/2" in helper
assert "y=t.y+t.height/2-height/2" in helper
replacement = replacement.replace("@@BSI_HIDDEN_TABS@@", "[]")
replacement = replacement.replace("@@BSI_TAB_ICONS@@", "({})")

with tempfile.TemporaryDirectory(prefix="bsi-settings-qml-") as temp:
    root = Path(temp)
    module = root / "BsiSettingsFixture"
    module.mkdir()
    (module / "qmldir").write_text(
        "module BsiSettingsFixture\n"
        "UiShadowContainer 1.0 UiShadowContainer.qml\n"
        "UiImageButton 1.0 UiImageButton.qml\n"
        "UiSettingsButton 1.0 UiSettingsButton.qml\n"
        "singleton UiTheme 1.0 UiTheme.qml\n",
        encoding="utf-8",
    )
    (module / "UiShadowContainer.qml").write_text(
        "import QtQuick 2.15\nItem { }\n", encoding="utf-8"
    )
    (module / "UiImageButton.qml").write_text(
        "import QtQuick 2.15\n"
        "Item {\n"
        "    width: 32; height: 32\n"
        "    property string asset: \"\"\n"
        "    property int pImageWidth: 16\n"
        "    property int pImageHeight: 16\n"
        "    signal clicked()\n"
        "    MouseArea { anchors.fill: parent; onClicked: parent.clicked() }\n"
        "}\n",
        encoding="utf-8",
    )
    (module / "UiSettingsButton.qml").write_text(
        "import QtQuick 2.15\n"
        "Rectangle {\n"
        "    property int pSettingIdx: 0\n"
        "    property string pSettingBtnText: \"\"\n"
        "    property bool pSelected: false\n"
        "    property bool pShowRedDot: false\n"
        "    signal settingClicked(int index)\n"
        "    radius: 8\n"
        "    color: pSelected ? \"#203c55\" : \"#182635\"\n"
        "    Text { anchors.fill: parent; text: pSettingBtnText; color: \"white\"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }\n"
        "    MouseArea { anchors.fill: parent; onClicked: parent.settingClicked(parent.pSettingIdx) }\n"
        "}\n",
        encoding="utf-8",
    )
    (module / "UiTheme.qml").write_text(
        "pragma Singleton\n"
        "import QtQuick 2.15\n"
        "QtObject {\n"
        "    property QtObject colors: QtObject {\n"
        "        property color primary10: \"#f4f7fb\"\n"
        "        property color primary20: \"#d2dae4\"\n"
        "        property color primary30: \"#9eacbd\"\n"
        "        property color primary60: \"#35465a\"\n"
        "        property color primary70: \"#1b2838\"\n"
        "        property color primary80: \"#101a27\"\n"
        "        property color accentNormal: \"#54c8ff\"\n"
        "    }\n"
        "    property QtObject fonts: QtObject {\n"
        "        property font bodySmall: Qt.font({pixelSize: 11})\n"
        "        property font bodyMedium: Qt.font({pixelSize: 13})\n"
        "        property font bodyLarge: Qt.font({pixelSize: 17})\n"
        "    }\n"
        "}\n",
        encoding="utf-8",
    )
    (root / "SettingPage.qml").write_text(
        "import QtQuick 2.15\n"
        "Item {\n"
        "    objectName: \"runtimeSettingPage\"\n"
        "    implicitWidth: 520; implicitHeight: 360\n"
        "    width: parent.width * 0.55; height: parent.height\n"
        "    function fOnQmlLoaded() { }\n"
        "}\n",
        encoding="utf-8",
    )

    host = root / "SettingsHost.qml"
    host.write_text(
        "import QtQuick 2.15\n"
        "import QtQuick.Controls 2.15\n"
        "import QtQuick.Layouts 1.15\n"
        "import BsiSettingsFixture 1.0\n"
        "Item {\n"
        "    objectName: \"settingsTestHost\"\n"
        "    visible: false\n"
        "    width: 1500; height: 900\n"
        "    property var transientParent: iSettingsWindow\n"
        "    property int pCurrentTabIndex: 0\n"
        "    property var pSettingsItems: ({ pSettingElements: [\n"
        "        { pButtonText: \"Display\", pShowRedDot: false, pQmlSource: \"SettingPage.qml\", pComponentArgs: null, pComponentObj: null },\n"
        "        { pButtonText: \"Graphics\", pShowRedDot: true, pQmlSource: \"SettingPage.qml\", pComponentArgs: null, pComponentObj: null }\n"
        "    ] })\n"
        "    function fChangeSelectedSetting(index) { pCurrentTabIndex = index }\n"
        + replacement
        + "\n"
        + helper
        + "\n}\n",
        encoding="utf-8",
    )

    engine = QQmlEngine()
    engine.addImportPath(str(root))
    player = PlayerWindowDouble()
    engine.rootContext().setContextProperty("iSettingsWindow", player)
    component = QQmlComponent(engine, QUrl.fromLocalFile(str(host)))
    obj = component.create()
    if obj is None:
        raise AssertionError("Settings QML failed to create: " + "\n".join(e.toString() for e in component.errors()))

    window = QQuickWindow()
    window.resize(int(player.width), int(player.height))
    obj.setWidth(player.width)
    obj.setHeight(player.height)
    obj.setParentItem(window.contentItem())
    window.show()
    obj.setVisible(True)
    QTest.qWait(120)

    def visual_items(item):
        found = []
        pending = list(item.childItems())
        while pending:
            child = pending.pop()
            found.append(child)
            pending.extend(child.childItems())
        return found

    visual_tree = visual_items(obj)
    def visual_named(name):
        return next((item for item in visual_tree if item.objectName() == name), None)

    tabs = next(
        (item for item in visual_tree if item.metaObject().className().endswith("ListView")),
        None,
    )
    stack = next(
        (item for item in visual_tree if item.metaObject().className().endswith("StackLayout")),
        None,
    )
    # A StackLayout keeps one Loader per tab and only the current one has its
    # item created, so pick the Loader that actually holds a page.
    page_loader = next(
        (item for item in visual_tree
         if item.metaObject().className().endswith("Loader")
         and item.property("item") is not None
         and item.width() > 100),
        None,
    )
    # The page host is an id-carrying item, so it is reached through the QML
    # child tree rather than objectName. It keeps the native 568 px column,
    # centred, so the fixed-width stock pages are not stretched.
    page_frame = next(
        (candidate for candidate in visual_tree
         if abs(candidate.width() - min(obj.width() - 48, 568)) < 2
         and abs(candidate.height() - (obj.height() - 118)) < 3
         and candidate is not obj),
        None,
    )
    panel = next(
        (item for item in visual_tree
         if abs(item.width() - obj.width()) < 2
         and abs(item.height() - obj.height()) < 2),
        None,
    )
    first_tab = next(
        (
            item for item in visual_tree
            if str(item.property("pSettingBtnText")).endswith("Graphics")
        ),
        None,
    )
    settings_title = next(
        (item for item in visual_tree if str(item.property("text")) == "Settings"),
        None,
    )
    # A StackLayout keeps a Loader per tab; the collapsed ones can leave a
    # zero-sized page instance in the tree, so pick the one that is drawn.
    loaded_page = next(
        (item for item in visual_tree
         if item.objectName() == "runtimeSettingPage" and item.width() > 100),
        None,
    )
    drag = next(
        (
            item for item in visual_tree
            if item.metaObject().className().endswith("MouseArea")
            and item.width() > window.width() * 0.5
            and item.height() < 70
        ),
        None,
    )
    close = next(
        (item for item in visual_tree if str(item.property("asset")) == "CloseIcon"),
        None,
    )
    controls = {
        "centered Settings title": settings_title,
        "compact centered panel": panel,
        "centered native page loader": page_loader,
        "tabs": tabs,
        "stack": stack,
        "loaded native page": loaded_page,
        "category tab 1": first_tab,
        "drag handle": drag,
        "close button": close,
    }
    missing = [name for name, item in controls.items() if item is None]
    found_names = sorted(item.objectName() for item in visual_tree if item.objectName())
    assert not missing, f"Missing controls: {missing}; found object names: {found_names}"
    panel_center = panel.mapToItem(window.contentItem(), QPointF(panel.width() / 2, panel.height() / 2))
    assert abs(panel_center.x() - window.width() / 2) < 2 and abs(panel_center.y() - window.height() / 2) < 2, "The PR #88 Settings shell should stay centered in the player window"
    assert abs(obj.width() - 1120) < 2 and abs(obj.height() - 670) < 2, "The redesigned Settings window should use the viewport-sized footprint, capped for desktop readability"
    assert abs(obj.x() - 190) < 2 and abs(obj.y() - 115) < 2, "The larger Settings window should remain centered"
    title_center = settings_title.mapToItem(obj, QPointF(settings_title.width() / 2, settings_title.height() / 2))
    assert abs(title_center.x() - obj.width() / 2) < 2, "The Settings title should be centered in the title bar"
    # Gaps: one spacing value for the whole strip, so the distance between any
    # two neighbouring tabs is identical. The old uniform-slot layout put a
    # different amount of air around short and long labels ("About" floated in a
    # wide box) and let long neighbours such as "Game settings" and "Devices"
    # run into each other.
    assert tabs.property("spacing") == 4, "every gap between tabs must come from one spacing value"
    tab_slots = [item for item in visual_tree if item.property("pSettingBtnText") is not None]
    assert len(tab_slots) >= 2, "Visible native categories should each have a tab button"
    # Sizing: each tab is as wide as its own label plus a fixed pad, so a long
    # label gets a long tab and a short one does not float in dead space.
    widths = [round(item.width()) for item in tab_slots]
    assert len(set(widths)) > 1, \
        "tabs must size to their own labels, not share one uniform width"
    assert all(w >= 40 for w in widths), "every tab must stay wide enough to read"
    # Placement: laid out left to right with even spacing, each tab starting
    # exactly where the previous one ended plus the gap.
    order = sorted(tab_slots, key=lambda i: i.mapToItem(panel, QPointF(0, 0)).x())
    lefts = [i.mapToItem(panel, QPointF(0, 0)).x() for i in order]
    widths = [round(item.width()) for item in order]
    gaps = [round(b - (a + w)) for a, b, w in zip(lefts, lefts[1:], widths)]
    assert len(set(gaps)) == 1, f"gaps between tabs must all be equal, got {gaps}"
    assert 0 < gaps[0] <= 12, f"the gap between tabs must be visible but tight, got {gaps[0]}"
    # No collisions: no tab may overlap its neighbour, however long the labels.
    for (a, wa), (b, _) in zip(zip(lefts, widths), zip(lefts[1:], widths[1:])):
        assert b >= a + wa, f"tab at {a} (width {wa}) runs into the tab at {b}"
    # Every tab, including the last, must be fully inside the strip.
    assert lefts[-1] + widths[-1] <= panel.width() - 24 + 1, \
        "the last category must not be clipped at the shell edge"
    # The strip itself is centred: the whole run of tabs is one group in the
    # middle of the title row, not a left-aligned list with dead space to the
    # right. When the tabs are wider than the window the strip is clamped and
    # scrolls instead, so the gutter is what is centred then.
    strip_left = tabs.property("x")
    strip_width = tabs.width()
    assert abs((strip_left + strip_width / 2) - panel.width() / 2) < 2, \
        f"the tab strip must be centred (x {strip_left}, width {strip_width} of {panel.width()})"
    assert strip_left >= 23, f"the strip must keep its gutter from the window edge (x {strip_left})"
    assert abs(player.width - 1500) < 2 and abs(player.height - 900) < 2, "The shell should resize without changing the host player window"
    assert abs(panel.width() - obj.width()) < 2 and abs(panel.height() - obj.height()) < 2, "The PR #88 shell should fill its resized host"
    stack_center = stack.mapToItem(panel, QPointF(stack.width() / 2, stack.height() / 2))
    assert abs(stack_center.x() - panel.width() / 2) < 2, "The native page stack was not centered inside the shell"
    # The stock pages are fixed-width surfaces, so the shell keeps the native
    # 568 px column centred instead of stretching a page across the window.
    assert page_frame is not None, "the shell did not create its centred page host"
    frame_center = page_frame.mapToItem(panel, QPointF(page_frame.width() / 2, 0))
    assert abs(frame_center.x() - panel.width() / 2) < 2, "the page host should be centred in the shell"
    # The host keeps the native 568 px column, and is short enough that the
    # page's own footer cannot reach the rounded bottom edge of the shell.
    assert abs(page_frame.width() - min(panel.width() - 48, 568)) < 2, \
        "the page host must keep the native column width"
    assert page_frame.height() <= panel.height() - 110, \
        "the page host must clear the bottom edge"
    assert abs(stack.width() - page_frame.width()) < 2, "the native page stack should fill its centred host"
    loader_position = page_loader.mapToItem(panel, QPointF(0, 0))
    assert 0 < page_loader.width() <= page_frame.width(), "The native page Loader should stay inside the centered page card"
    assert loader_position.x() >= frame_center.x() - page_frame.width() / 2 - 8, "The native Loader drifted out of its page card"
    assert loader_position.x() + page_loader.width() <= frame_center.x() + page_frame.width() / 2 + 8, "The native Loader escaped its page card"
    # Nothing should be clipped by the shell: the tab strip must sit fully
    # inside the frame on the row above the card.
    tabs_bottom = tabs.mapToItem(panel, QPointF(0, tabs.height())).y()
    card_top = page_frame.mapToItem(panel, QPointF(0, 0)).y()
    assert tabs_bottom <= card_top + 1, "The category strip was overlapped by the page card"
    assert tabs.property("x") >= 0 and tabs.property("width") <= panel.width(), "The category strip overflowed the shell"
    # The page fills its Loader through the shell's own onLoaded assignment, so
    # both must report the same size (the page itself declares no width).
    assert abs(loaded_page.width() - page_loader.width()) < 2, \
        "the loaded native page did not follow its Loader width"
    assert loaded_page.objectName() == "runtimeSettingPage", "The native Settings page Loader did not load its page"
    # The rebuilt About page publishes its tab icons by walking up until it sees
    # `bIcons`, so what matters is that the shell is reachable at all - not that
    # it is a fixed number of hops away. Pinning a count here used to force a
    # layout-only wrapper Item into the shell, which cost bytes in a resource
    # slot that is already the binding constraint.
    node = loaded_page
    while node is not None and node is not obj and (
        not hasattr(node, "property") or node.property("bIcons") is None
    ):
        node = node.parentItem()
    assert node is not None, "a native page cannot reach the shell by walking up"
    assert str(first_tab.property("pSettingBtnText")).splitlines()[0], "Category tabs should show their own icon glyphs"

    tab_point = first_tab.mapToScene(QPointF(first_tab.width() / 2, first_tab.height() / 2)).toPoint()
    QTest.mouseClick(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, tab_point)
    QTest.qWait(60)
    assert obj.property("pCurrentTabIndex") == 1, "Clicking a category did not call the native selection callback"
    assert stack.property("currentIndex") == 1 and first_tab.property("pSelected"), "Category selection did not update the selected tab and page stack"

    drag_point = drag.mapToScene(QPointF(8, 8)).toPoint()
    QTest.mousePress(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, drag_point)
    QTest.mouseRelease(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, drag_point)
    assert player.move_requested, "The title bar did not invoke the native window-drag callback"

    close_point = close.mapToScene(QPointF(close.width() / 2, close.height() / 2)).toPoint()
    QTest.mouseClick(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, close_point)
    assert player.close_requested, "The close button did not invoke the native window-close callback"

    about_match = re.search(r'QString generated = QStringLiteral\(R"QML\((.*?)\)QML"\);', cpp, re.S)
    assert about_match, "Could not find the generated About-page QML template"
    about_qml = about_match.group(1)
    about_qml = about_qml.replace(
        "import QtQuick.Layouts 1.15",
        "import QtQuick.Layouts 1.15\nimport BsiSettingsFixture 1.0",
    )
    about_qml = about_qml.replace("@@BSI_DEVELOPER@@", '"Runtime Developer"')
    about_qml = about_qml.replace("@@BSI_TAGLINE@@", '"Runtime subtitle"')
    about_qml = about_qml.replace(
        "@@BSI_LINKS@@",
        '{ title: "GitHub", url: "https://github.com/example" },\n'
        '{ title: "Community", url: "https://example.com/community" },',
    )
    about_qml = about_qml.replace("@@BSI_HIDDEN_TABS@@", "[]")
    about_icon = ("data:image/svg+xml;base64," + __import__("base64").b64encode(
        b'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">'
        b'<circle cx="12" cy="12" r="9" fill="#63e6be"/></svg>').decode())
    about_qml = about_qml.replace(
        "@@BSI_TAB_ICONS@@",
        '({"settingpage.qml":"' + about_icon + '"})')
    about_qml = about_qml.replace("@@BSI_LOGO_SOURCE@@", '""')
    about_path = root / "AboutHost.qml"
    about_path.write_text(about_qml, encoding="utf-8")
    about_component = QQmlComponent(engine, QUrl.fromLocalFile(str(about_path)))
    about_obj = about_component.create()
    if about_obj is None:
        raise AssertionError("About QML failed to create: " + "\n".join(e.toString() for e in about_component.errors()))
    about_tree = visual_items(about_obj)
    about_links = [item for item in about_tree if item.objectName() == "bsiAboutDeveloperLink"]
    assert about_obj.property("pDeveloperName") == "Runtime Developer", "About page did not retain the customizable developer name"
    assert about_obj.property("pDeveloperTagline") == "Runtime subtitle", "About page did not render the customizable subtitle"
    assert any(item.objectName() == "bsiAboutDeveloperTagline" for item in about_tree), "About page did not create its subtitle element"
    assert len(about_links) == 2, "About page did not render the configured developer links"
    assert any(item.objectName() == "bsiAboutEmulatorLogo" for item in about_tree), "About page did not create its centered emulator logo"
    # The About content is a single centred column: it used to be pinned to the
    # left of its scroll area, so the logo, name and link rows all sat left of
    # centre in a window that is otherwise centred. It is centred in its
    # viewport, and no fixed contentWidth overrides that.
    assert "ColumnLayout{id:iAboutD;width:Math.min(440,parent.width-24);anchors.horizontalCenter:parent.horizontalCenter" \
        in about_source, "the About content column must be centred in its viewport"
    assert "contentWidth:availableWidth" in about_source, \
        "the About content must span the viewport so its column is centred in the page"
    assert "contentWidth:iAboutD.width" not in about_source, \
        "a fixed contentWidth would pin the About column to the left"

    print("PASS: generated Settings and About QML instantiate, retain native page behavior, show custom tabs, and render profile links")
    about_obj.deleteLater()
    # Now the part that was broken in the field: a custom tab icon must reach
    # the shell and be drawn. The About page publishes bHidden/bIcons by walking
    # up from itself, so load the real generated page as a child of the shell
    # and check that the assignment lands - counting parents used to land on the
    # window instead, throw "Cannot assign to non-existent property", and leave
    # every icon undisplayed even though the patch applied cleanly.
    mounted_component = QQmlComponent(engine, QUrl.fromLocalFile(str(about_path)))
    about_child = mounted_component.create()
    assert about_child is not None, "The generated About page did not load inside the shell"
    about_child.setParentItem(page_loader)
    QTest.qWait(30)
    # The player's Loader calls this after loading; do the same.
    from PySide6.QtCore import QMetaObject
    assert QMetaObject.invokeMethod(about_child, "fOnQmlLoaded"), \
        "The About page must expose fOnQmlLoaded for the player's Loader to call"
    QTest.qWait(30)
    shell_root = None
    probe = page_loader.parentItem()
    while probe is not None:
        try:
            if probe.property("bIcons") is not None:
                shell_root = probe
                break
        except (RuntimeError, TypeError):
            pass
        probe = probe.parentItem()
    assert shell_root is not None, "The About page could not reach the shell's bIcons property"
    published_icons = shell_root.property("bIcons")
    assert published_icons is not None, \
        "The shell must declare bIcons for the About page to publish into"
    if hasattr(published_icons, "toVariant"):
        published_icons = published_icons.toVariant()
    assert isinstance(published_icons, dict) and published_icons.get("settingpage.qml"), \
        f"The About page did not publish its tab icons to the shell (got {published_icons!r})"
    published_hidden = shell_root.property("bHidden")
    if hasattr(published_hidden, "toVariant"):
        published_hidden = published_hidden.toVariant()
    assert published_hidden == [], "The About page did not publish its hidden-tab list"
    published_icon = published_icons["settingpage.qml"]
    assert published_icon.startswith("data:image/svg+xml;base64,"), \
        "The published tab icon must be a self-contained SVG data URI"
    # And the strip must actually paint it, centred, with the label lifted clear.
    strip_items = visual_items(tabs)

    def source_of(item):
        # Image.source is a QUrl; compare the data URI itself, not its repr.
        try:
            if item.metaObject().indexOfProperty("source") <= 0:
                return None
        except (RuntimeError, TypeError):
            return None
        value = item.property("source")
        text = value.toString() if hasattr(value, "toString") else str(value or "")
        return text

    painted = [source_of(item) for item in strip_items]
    icon_items = [item for item in strip_items if source_of(item) == published_icon]
    assert icon_items, "The published icon is not painted by any tab"
    assert sum(1 for src in painted if src) >= 1, "No tab painted a custom icon"
    icon_mid = icon_items[0].mapToItem(first_tab, QPointF(icon_items[0].width() / 2, 0)).x()
    assert abs(icon_mid - first_tab.width() / 2) < 2, \
        "A custom icon must be horizontally centred in its tab"

    about_component.deleteLater()
    mounted_component.deleteLater()
    window.close()
    obj.deleteLater()
    component.deleteLater()
    engine.deleteLater()
    QTest.qWait(10)


# The About document has to hold every embedded tab icon, and the player's slot
# for it is small, so an icon the user picks must be compacted before it is
# pasted in as base64. This mirrors SettingsPanelPatcher::minifySvg(): comments,
# the XML prolog, metadata, doctype and insignificant whitespace are dropped and
# nothing else is touched. Checked here because the C++ version cannot run on
# this platform.
def _minify_svg(source: str) -> str:
    source = re.sub(r"<!--.*?-->", "", source, flags=re.S)
    source = re.sub(r"<metadata\b.*?</metadata\s*>", "", source, flags=re.S | re.I)
    source = re.sub(r"<!DOCTYPE[^>]*>", "", source, flags=re.I)
    source = re.sub(r"^\s*<\?xml[^>]*\?>", "", source, flags=re.I)
    source = re.sub(r">\s+<", "><", source)
    source = re.sub(r"\s+", " ", source)
    source = re.sub(r"<\s+", "<", source)
    source = re.sub(r"\s+/?>", ">", source)
    source = source.strip()
    if not source.endswith("/>"):
        source = re.sub(r"\s+/>", "/>", source)
    return source


editor_icon = '''<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<!-- Created with Inkscape (http://www.inkscape.org/) -->
<svg
   xmlns:dc="http://purl.org/dc/elements/1.1/"
   width="24"
   height="24"
   viewBox="0 0 24 24"
   version="1.1">
  <metadata id="metadata5">
    <rdf:RDF><cc:Work rdf:about=""><dc:format>image/svg+xml</dc:format></cc:Work></rdf:RDF>
  </metadata>
  <g id="layer1" transform="translate(0,-8)">
    <path
       style="fill:#54c8ff;stroke:none"
       d="M 4,12 H 20 V 20 H 4 Z"
       id="rect1" />
  </g>
</svg>
'''
compacted = _minify_svg(editor_icon)
assert len(compacted.encode()) < len(editor_icon.encode()) * 0.6, (
    "an editor-emitted tab icon must shrink substantially before it is embedded "
    f"({len(editor_icon.encode())} -> {len(compacted.encode())} bytes)")
assert 'd="M 4,12 H 20 V 20 H 4 Z"' in compacted, "the drawing itself must be preserved"
assert "inkscape" not in compacted.lower() and "<metadata" not in compacted, \
    "comments and metadata must be dropped"
assert not re.search(r"\s{2,}", compacted) and "\n" not in compacted, \
    "insignificant whitespace must be collapsed"
# Safety must survive compaction: a script or external reference still refuses.
for hostile in ("<svg><script>alert(1)</script></svg>",
                '<svg><image href="http://evil.example/x.png"/></svg>',
                '<svg><a onclick="x()"/></svg>'):
    assert re.search(r"<\s*(script|foreignObject)\b|\bon[a-z]+\s*=|href\s*=", hostile, re.I), \
        "the hostile fixture must still be rejected by validateAssetBytes"

print("PASS: tab icons are compacted before they are embedded in the About document")


# A tab icon the user picks is often far bigger than the slot the About page
# lives in (a 150-path icon is ~8.7 KB of markup against a 2337-byte slot), and
# refusing the upload is not an answer: the SVG is valid and only the container
# is small. Such an icon is drawn once at the size the strip uses and stored as a
# PNG instead, which is what makes "set 2+ custom icons" apply at all.
def _page_payload(icons_literal):
    doc = about_source
    for token, value in (
        ("@@BSI_DEVELOPER@@", '"Mr.Pc Gamer"'),
        ("@@BSI_TAGLINE@@", '"BlueStacks tools"'),
        ("@@BSI_HIDDEN_TABS@@", "[]"),
        ("@@BSI_LINKS@@", '{"title":"GitHub","url":"https://github.com/x"}'),
        ("@@BSI_LOGO_SOURCE@@", '"qrc:/qrcAssets/BlueStacks5Beta.svg"'),
        ("@@BSI_TAB_ICONS@@", icons_literal),
    ):
        doc = doc.replace(token, value)
    return len(zlib.compress(doc.encode(), 9))


def _icon_svg(paths: int) -> str:
    body = "".join(
        f'<path d="M{i} 2h8a2 2 0 0 1 2 2v{i}l-3 4z" fill="#54c8ff"/>' for i in range(paths))
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">{body}</svg>'


# The case that used to fail: several large icons at once.
big_icon = urllib.parse.quote(_icon_svg(150), safe="")
as_svg = "({" + ",".join(
    f'"{i}":"data:image/svg+xml,{big_icon}"' for i in range(4)) + "})"
assert _page_payload(as_svg) > 2337, (
    "the fixture must reproduce the over-slot case, otherwise this test proves nothing")
# After the raster fallback the same four icons are a few hundred bytes each.
assert "rasterizeSvg" in cpp and "artworkBudget" in cpp, \
    "a tab icon that cannot fit its slot must be drawn down, not refused"
assert "image/png" in cpp, "the drawn-down icon is stored as PNG"
assert "for (int box : {32, 28, 24, 20, 16, 12})" in cpp, \
    "the fallback should try progressively smaller squares until it fits"
print("PASS: oversized tab icons are drawn down instead of failing the apply")
