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
import xml.etree.ElementTree as ET
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QSG_RHI_BACKEND", "software")

from PySide6.QtCore import QObject, QPointF, Property, Qt, Slot, QUrl
from PySide6.QtGui import QColor, QGuiApplication, QImage
from PySide6.QtQuick import QQuickItem, QQuickWindow
from PySide6.QtQml import QQmlComponent, QQmlEngine, QQmlExpression
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
assert "BSI v21" in replacement
assert "pSettingsItems.pSettingElements" in replacement
# The About page publishes the hidden-tab list and the icon map to the shell.
# It must find that shell by walking up: counting parents landed on the window
# instead of the UiShadowContainer, which threw
# "Cannot assign to non-existent property bHidden" and silently dropped every
# custom tab icon (the file applied, nothing ever showed).
assert "function fOnQmlLoaded(){var p=parent;while(p&&p.bIcons===undefined)p=p.parent;if(p)p.bIcons=bsiTabIcons;Qt.callLater(fGlassTabs)}" in cpp, \
    "the About page must walk up to the shell, not count parents"
# One map now carries both icons and hidden sentinels. Retiring the unused
# bHidden channel buys room for the selection effect in legacy resource slots.
assert "property var bHidden" not in replacement, "hidden sentinels use the single icon-map channel"
assert "property var bIcons:({})" in replacement, "the shell must declare bIcons"
assert "property var bsiHiddenTabs:" not in cpp, "the unused hidden list must not duplicate numeric icon-map sentinels"
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
assert "property var c:UiTheme.colors" in replacement
assert "color:r.c.primary80" in replacement
assert "color:r.c.primary10" in replacement
assert "#0d0f14" not in replacement and 'color:"white"' not in replacement, \
    "the rebuilt shell must not hard-code colours"
assert 'color:"' not in replacement, "no literal colours may be baked into the shell"
assert "MouseArea{anchors.fill:parent;onPressed:iSettingsWindow.startSystemMove()}" in replacement, \
    "the whole header band must drag the window"
assert "anchors.centerIn:parent" in replacement, \
    "the title must be centred in the header band"
assert "ListView{width:Math.max(0,Math.min(contentWidth,r.width-48));height:44" in replacement
assert "orientation:Qt.Horizontal;model:pSettingsItems.pSettingElements" in replacement
# The strip spans the shell with a symmetric 24 px inset so the first and last
# tabs sit equally far from the window edge.
assert "x:(r.width-width)/2" in replacement, \
    "the tab strip must be centred in the window"
assert "iSettingsWindowShadowContainer.bIcons[" in replacement
assert "r.parent.bIcons" not in replacement, "native shadow containers reparent their default children"
# The icon map is keyed by the tab's own index, not by a name derived from its
# page path. `pQmlSource` is a QML `url`, and a url stringifies percent-encoded
# (qrc:%5CSettings%5CPerformanceSetting.qml): calling .toLowerCase() on the url
# itself threw "Property 'toLowerCase' ... is not a function", so the binding
# failed and *no* tab ever got an icon — and even stringified, splitting on a
# path separator could never match, because the backslash arrives as %5C.
# Keying by index removes the string surgery from the player entirely.
assert "bIcons[index]" in replacement, (
    "the strip delegate must look its icon up by tab index; deriving a key from "
    "pQmlSource cannot work because the value is a percent-encoded url"
)
assert ".toLowerCase()" not in replacement.split("bIcons[")[1].split("]")[0], \
    "no string method may be called on the url that indexes the icon map"
# Hidden tabs share that map, with an empty value as the sentinel, so the
# published keys must be tab indices on both paths.
_hidden_sentinel = 'QStringLiteral(":\\"\\"")'
assert _hidden_sentinel in cpp, (
    "a hidden tab must still publish the empty (collapsed, unpainted) sentinel"
)
# Artwork that cannot fit the slot is written beside the player and referenced,
# instead of failing the whole apply with "needs N bytes but its slot only holds
# M". Base64 defeats compression, so embedding capped custom artwork at roughly
# 600 bytes per slot; a file reference costs the document a short path.
assert "QString writeAssetFile(" in cpp and "QUrl::fromLocalFile(file).toString(QUrl::FullyEncoded)" in cpp, \
    "referenced artwork must use correctly encoded absolute file URLs"
assert "aboutFitProblem(image," in cpp, \
    "the apply must test the real document against the real slot before choosing"
assert "StagedAssetFiles stagedAssets" in cpp and "if (!committed)" in cpp, \
    "a refused apply must remove only files newly created for that attempt"
assert "sha256(bytes).left(24)" in cpp and "existing != bytes" in cpp, \
    "replacement images need immutable URLs so failed applies cannot overwrite live artwork"
assert 'settings-panel-assets' in cpp, \
    "assets belong in the app's own data folder, never inside the player's"
assert "customTabIcons = byTabIndex(customTabIcons, current.tabs);" in cpp, \
    "the embedded icon map must be re-keyed by tab index before it is published"
assert "byFile = byTabIndex(byFile, current.tabs);" in cpp, \
    "file references must use numeric keys too, not leave an unloadable data URI in the numeric slot"
assert 'bestLogoFiles.value(QStringLiteral("logo")).toMap()' in cpp, \
    "the logo asset must be unpacked before its bytes are written"
assert "runtimeAssetPng(it.value().toMap(), true)" in cpp, \
    "runtime icons must not depend on the player's optional SVG decoder"
assert "QXmlStreamReader xml(bytes)" in cpp and "!QSvgRenderer(bytes).isValid()" in cpp, \
    "malformed SVGs must fail when selected rather than apply as blank icons"
assert "hiddenTabs = keys.keys();" in cpp, \
    "the hidden-tab list must be re-keyed by tab index too, or hidden tabs reappear"
# Own the actual label instead of relying on UiSettingsButton's private,
# left-aligned column. Measure the exact same themed font that Text draws.
assert "UiSettingsButton{" not in replacement, \
    "the stock tab button's private label is left aligned"
assert "Text{id:t;text:modelData.pButtonText;font:h.font;" in replacement
assert "font:UiTheme.fonts.bodyLarge;anchors.centerIn:parent" in replacement
assert "TextMetrics{id:m;text:t.text;font:h.font}" in replacement, \
    "width and bearing must use the actual label's font and text"
assert "width:c===''?-4:m.boundingRect.width+22;height:44" in replacement, \
    "each hit target must fit its own label plus 11 px on each side"
assert "x:11-m.boundingRect.x" in replacement, \
    "cancel glyph bearings so the label's ink, not just its Text box, is centred"
assert "advanceWidth(" not in replacement
assert ";spacing:4" in replacement, \
    "all hit-target gaps must be exactly 4 px (all label gaps exactly 26 px)"
assert "y:c?0:22-height/2" in replacement, \
    "labels must be centred vertically or lifted above their icon"
assert "ToolButton{width:18;height:18;x:parent.width/2-9;y:24;enabled:false;padding:0;background:null;icon{width:18;height:18;color:index==pCurrentTabIndex?t.color:'transparent';source:c||''}}" in replacement
# The badge must not shift the label, and the full padded tab remains clickable.
assert "x:parent.width-8;y:t.y+6;width:6;height:6" in replacement
assert "visible:modelData.pShowRedDot" in replacement
assert "onClicked:{fChangeSelectedSetting(index);forceActiveFocus()}" in replacement
assert "delegate:MouseArea{hoverEnabled:true;visible:c!=='';" in replacement, \
    "a hidden tab must neither paint nor intercept clicks"
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

# Every category must stay reachable by scrolling when the strip overflows.
assert "item.anchors.fill=item.parent" in replacement
# The native page is still a flat centred column, not an extra content card.
# The latest request explicitly adds a selected-tab effect, including its icon.
assert "Rectangle{color:r.color;width:Math.min(r.width-48,568);x:(r.width-width)/2;height:r.height-118" in replacement, \
    "the full native page/footer must have a flat opaque foundation inside the rounded window"
assert "radius:12" not in replacement, "do not wrap the native page in a new card"
assert "Rectangle{anchors.fill:parent;radius:6;color:t.color;opacity:index==pCurrentTabIndex?.2:parent.containsMouse?.1:0}" in replacement, \
    "selection and quieter hover must use the label's native theme colour"
assert "icon{width:18;height:18;color:index==pCurrentTabIndex?t.color:'transparent'" in replacement, \
    "tint the actual selected icon and preserve custom colours when deselected"
assert "Rectangle{anchors.fill:parent;radius:6;border.color:index==pCurrentTabIndex?t.color:'transparent';color:'transparent'}" in replacement, \
    "the selected tab needs a separate full-opacity themed outline; hover must not acquire one"
assert "GraphicalEffects" not in replacement and "ShaderEffect" not in replacement, \
    "selection must work on both player Qt generations without optional effects"
assert "Rectangle{id:r;property var c:UiTheme.colors;anchors.fill:parent;radius:14;color:r.c.primary80" in replacement, \
    "the window keeps its native base colour; the About-hosted material decorates that surface"
assert "StackLayout{anchors.fill:parent;currentIndex:pCurrentTabIndex" in replacement
# Keep a coarse template-bloat guard, but this is not proof that the compressed
# whole window fits: native callbacks/prefix and compression entropy matter too.
# settings_panel_window_budget_test reconstructs the complete 2468-byte legacy
# resource, including the original native prefix and the geometry helper.
assert len(replacement.encode()) <= 2080, (
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
assert about_payload <= 1950, (
    f"the About page compresses to {about_payload} bytes with one embedded icon; "
    "the smallest known player slot is 2337 bytes and a logo goes in the same "
    "document; the complete 12/13-icon cases are separately pinned below 2337"
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
assert "delegate:Loader{Component.onCompleted:setSource(modelData.pQmlSource" in replacement
assert "text:modelData.pButtonText" in replacement
assert "color:r.c[index==pCurrentTabIndex?'subWindowHeaderClick':'primary20']" in replacement
assert "visible:modelData.pShowRedDot" in replacement
assert "fChangeSelectedSetting(index)" in replacement

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
assert helper_match, "Could not find the compact Settings-window geometry helper"
helper = helper_match.group(1)
assert "onVisibleChanged:if(visible&&transientParent)" in helper
assert "var t=transientParent" in helper
assert "width=Math.min(t.width*.96,780)" in helper
assert "height=Math.min(t.height*.94,500)" in helper
assert "x=(t.width-width)/2+t.x" in helper
assert "y=(t.height-height)/2+t.y" in helper
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
        "singleton UiTheme 1.0 UiTheme.qml\n",
        encoding="utf-8",
    )
    (module / "UiShadowContainer.qml").write_text(
        "import QtQuick 2.15\n"
        "Item { default property alias children: content.data\n"
        "    Item { id: content; anchors.fill: parent }\n"
        "}\n", encoding="utf-8"
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
        "        property color subWindowHeaderClick: \"#54c8ff\"\n"
        "        property color dangerNormal: \"#ee3344\"\n"
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

    # The shipping About material listens to this native Window property. A
    # bare C++ QQuickWindow has no matching signal, unlike the player's root.
    window_component = QQmlComponent(engine)
    window_component.setData(b'import QtQuick.Window 2.15; Window { property int pCurrentTabIndex: 0 }', QUrl("SettingsWindowContract.qml"))
    window = window_component.create()
    assert window is not None, [e.toString() for e in window_component.errors()]
    window.resize(int(player.width), int(player.height))
    obj.setWidth(player.width)
    obj.setHeight(player.height)
    obj.setParentItem(window.contentItem())
    obj.setParent(window)
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
    first_label = next(
        (
            item for item in visual_tree
            if str(item.property("text")) == "Graphics"
        ),
        None,
    )
    first_tab = first_label.parentItem() if first_label is not None else None
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
            and abs(item.width() - obj.width()) < 2
            and abs(item.height() - 56) < 2
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
    assert abs(panel_center.x() - window.width() / 2) < 2 and abs(panel_center.y() - window.height() / 2) < 2, "The compact Settings shell should stay centered in the player window"
    assert abs(obj.width() - 780) < 2 and abs(obj.height() - 500) < 2, "Settings must trim the marked side space while retaining the native shadow allowance"
    assert abs(obj.x() - 360) < 2 and abs(obj.y() - 200) < 2, "The screenshot-sized Settings window should remain centered"
    title_center = settings_title.mapToItem(obj, QPointF(settings_title.width() / 2, settings_title.height() / 2))
    assert abs(title_center.x() - obj.width() / 2) < 2, "The Settings title should be centered in the title bar"
    # Gaps: one spacing value for the whole strip, so the distance between any
    # two neighbouring tabs is identical. The old uniform-slot layout put a
    # different amount of air around short and long labels ("About" floated in a
    # wide box) and let long neighbours such as "Game settings" and "Devices"
    # run into each other.
    assert tabs.property("spacing") == 4, "every gap between tabs must come from one spacing value"
    tab_labels = [item for item in visual_tree if item.property("text") in ("Display", "Graphics")]
    tab_slots = [item.parentItem() for item in tab_labels]
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
    page_top = loaded_page.mapToItem(panel, QPointF(0, 0)).y()
    assert abs(page_top - tabs_bottom - 12) < 2, "The native page must start 12 px below the tab strip"
    assert abs(stack.height() - (page_frame.height() - 12)) < 2, "The new top gap must not push the native footer down"
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
    assert first_label.property("text") == "Graphics", "Category tabs must retain their native labels"

    tab_point = first_tab.mapToScene(QPointF(first_tab.width() / 2, first_tab.height() / 2)).toPoint()
    QTest.mouseClick(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, tab_point)
    QTest.qWait(60)
    assert obj.property("pCurrentTabIndex") == 1, "Clicking a category did not call the native selection callback"
    assert stack.property("currentIndex") == 1, "Category selection did not update the native page stack"
    assert first_label.property("color") == QColor("#54c8ff"), "Selection must use the player's selected-tab text colour"
    assert first_tab.hasActiveFocus(), "Clicking a category must retain native focus behaviour"

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
        "import QtQuick.Controls 2.15",
        "import QtQuick.Controls 2.15\nimport BsiSettingsFixture 1.0",
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
    # Publish the icon the way SettingsPanelPatcher::apply() now does: keyed by
    # the tab's own index. A key derived from pQmlSource cannot work, because the
    # value is a percent-encoded url. "SettingPage.qml" is the first tab here.
    # The delegate indexes with its own `index`, so the fixture must publish
    # under that same number rather than a page name.
    about_qml = about_qml.replace(
        "@@BSI_TAB_ICONS@@",
        '({"0":"' + about_icon + '"})')
    about_qml = about_qml.replace("@@BSI_LOGO_SOURCE@@", '""')
    about_path = root / "AboutHost.qml"
    about_path.write_text(about_qml, encoding="utf-8")
    about_component = QQmlComponent(engine, QUrl.fromLocalFile(str(about_path)))
    about_obj = about_component.create()
    if about_obj is None:
        raise AssertionError("About QML failed to create: " + "\n".join(e.toString() for e in about_component.errors()))
    about_tree = visual_items(about_obj)
    about_links = [item for item in about_tree if item.objectName() == "bsiAboutDeveloperLink"]
    assert next(i for i in about_tree if i.objectName() == "bsiAboutDeveloperName").property("text") == "Runtime Developer", "About page did not retain the customizable developer name"
    assert next(i for i in about_tree if i.objectName() == "bsiAboutDeveloperTagline").property("text") == "Runtime subtitle", "About page did not render the customizable subtitle"
    assert any(item.objectName() == "bsiAboutDeveloperTagline" for item in about_tree), "About page did not create its subtitle element"
    assert len(about_links) == 2, "About page did not render the configured developer links"
    assert any(item.objectName() == "bsiAboutEmulatorLogo" for item in about_tree), "About page did not create its centered emulator logo"
    # The About content is a single centred column: it used to be pinned to the
    # left of its scroll area, so the logo, name and link rows all sat left of
    # centre in a window that is otherwise centred. It is centred in its
    # viewport, and no fixed contentWidth overrides that.
    assert "Column{id:d;width:Math.min(440,parent.width-24);anchors.horizontalCenter:parent.horizontalCenter" \
        in about_source, "the About content column must be centred in its viewport"
    assert "contentWidth:availableWidth" in about_source, \
        "the About content must span the viewport so its column is centred in the page"
    assert "contentWidth:iAboutD.width" not in about_source, \
        "a fixed contentWidth would pin the About column to the left"

    # A real decoded image has implicit dimensions: an empty source cannot
    # reproduce the oversized About artwork reported in a compact window.
    # Mount the actual generated About page in the currently visible Loader,
    # just as the player does, and exercise square/wide/tall high-res artwork.
    active_loader = next(item for item in visual_items(obj)
                         if item.metaObject().className().endswith("Loader")
                         and item.isVisible() and item.width() > 100)
    about_obj.setParentItem(active_loader)
    about_obj.setParent(active_loader)
    about_obj.setWidth(active_loader.width())
    about_obj.setHeight(active_loader.height())
    logo = next(item for item in about_tree if item.objectName() == "bsiAboutEmulatorLogo")
    name = next(item for item in about_tree if item.objectName() == "bsiAboutDeveloperName")
    for label, width, height in [("square", 320, 320), ("wide", 1280, 320),
                                 ("tall", 320, 1280), ("large", 2048, 2048)]:
        image_path = root / ("about logo " + label + ".png")
        image = QImage(width, height, QImage.Format.Format_ARGB32)
        image.fill(QColor("#e7e9f2"))
        assert image.save(str(image_path))
        logo.setProperty("source", QUrl.fromLocalFile(str(image_path)))
        QTest.qWait(80)
        ready = QQmlExpression(engine.rootContext(), logo, "status === 1")
        result = ready.evaluate()
        assert not ready.hasError() and (result[0] if isinstance(result, tuple) else result), \
            "the About logo fixture did not decode"
        assert 0 < logo.width() <= 160.1 and 0 < logo.height() <= 60.1, \
            f"{label} About artwork escaped its compact layout bounds: {logo.width()}x{logo.height()}"
        painted_width, painted_height = logo.property("paintedWidth"), logo.property("paintedHeight")
        assert 0 < painted_width <= 160.1 and 0 < painted_height <= 60.1
        assert abs(painted_width / painted_height - width / height) < .01, \
            "the About logo must retain the supplied artwork's aspect ratio"
        center = logo.mapToItem(about_obj, QPointF(logo.width() / 2, 0)).x()
        assert abs(center - about_obj.width() / 2) < 2, "the smaller About logo must stay centered"
        logo_bottom = logo.mapToItem(about_obj, QPointF(0, logo.height())).y()
        name_top = name.mapToItem(about_obj, QPointF(0, 0)).y()
        assert logo_bottom <= name_top and name_top < about_obj.height(), \
            "the About logo must not cover or push the developer name out of the compact page"
    print("PASS: real square, wide, tall and oversized About logos stay centered within 160x60 without stretching")

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
    about_child.setParent(page_loader)
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
    assert isinstance(published_icons, dict) and published_icons.get("0"), \
        f"The About page did not publish its tab icons to the shell (got {published_icons!r})"
    assert shell_root.metaObject().indexOfProperty("bHidden") < 0, \
        "the unused hidden-list channel must not return; hidden sentinels are tested below"
    published_icon = published_icons["0"]
    assert published_icon.startswith("data:image/svg+xml;base64,"), \
        "The published tab icon must be a self-contained SVG data URI"
    # ...unless the player runs Qt 5. A QML Image cannot open a data: URI on Qt 5
    # (measured: status Error, while the identical bytes load from a file), so the
    # apply must publish a file:// path there instead. This fixture is Qt 6, so the
    # check below is that the code knows to differ, not that this case differs.
    assert "int playerQtMajor(const QByteArray &image)" in cpp, \
        "the apply must read the player's own Qt version from its import table"
    assert "mustReference = qtMajor != 6" in cpp, \
        "Qt 5 and unidentified players must get referenced icons and logos, never data URIs"
    assert "QUrl::fromLocalFile(file).toString(QUrl::FullyEncoded)" in cpp, \
        "referenced icons must be encoded absolute file URLs the player can open"
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
    # The icon is 18x18 at x = parent.width/2 - 9, inside the delegate's own
    # Item, so its centre is the delegate's centre. Measure both against the same
    # item rather than across the button, which is inset and taller.
    icon = icon_items[0]
    icon_centre = icon.mapToItem(strip_items[0].parentItem(), QPointF(icon.width() / 2, 0)).x()
    delegate = icon.parentItem()
    delegate_centre = delegate.mapToItem(strip_items[0].parentItem(),
                                         QPointF(delegate.width() / 2, 0)).x()
    assert icon.width() == 18 and icon.height() == 18, \
        f"a custom icon must be drawn at 18x18, got {icon.width()}x{icon.height()}"
    assert abs(icon_centre - delegate_centre) < 2, (
        f"a custom icon must be horizontally centred in its tab "
        f"(icon centre {icon_centre}, tab centre {delegate_centre})"
    )

    # Exercise the referenced runtime form as well, using a path that cannot be
    # made into a URL by concatenating "file:///". The PNG must actually decode,
    # not merely appear in Image.source (which also happens for broken images).
    runtime_dir = root / "icons # space % café"
    runtime_dir.mkdir()
    runtime_path = runtime_dir / "replacement icon.png"
    runtime_image = QImage(54, 54, QImage.Format.Format_ARGB32)
    runtime_image.fill(QColor("#33dd77"))
    assert runtime_image.save(str(runtime_path))
    runtime_url = QUrl.fromLocalFile(str(runtime_path)).toString(QUrl.ComponentFormattingOption.FullyEncoded)
    assert about_child.setProperty("bsiTabIcons", {"0": runtime_url, "1": ""})
    assert QMetaObject.invokeMethod(about_child, "fOnQmlLoaded")
    QTest.qWait(60)
    runtime_icons = [item for item in visual_items(tabs)
                     if source_of(item) and QUrl(source_of(item)).toLocalFile() == str(runtime_path)]
    assert runtime_icons, "the referenced PNG did not reach the numeric tab slot"
    decoded = QQmlExpression(engine.rootContext(), runtime_icons[0], "status === 1")  # Image.Ready
    result = decoded.evaluate()
    assert not decoded.hasError() and (result[0] if isinstance(result, tuple) else result), \
        "the referenced PNG must decode as Image.Ready, including spaces, #, % and Unicode in its URL"
    hidden_label = next(item for item in visual_items(tabs) if item.property("text") == "Graphics")
    hidden_tab = hidden_label.parentItem()
    assert not hidden_tab.isVisible(), "hidden sentinels must still work beside file-backed icons"
    print("PASS: referenced PNG icons decode from encoded local URLs and keep hidden-tab sentinels")

    about_component.deleteLater()
    mounted_component.deleteLater()
    window.close()
    window.deleteLater()
    component.deleteLater()
    # Destroy mounted objects before their engine-owned theme singleton.
    from PySide6.QtCore import QCoreApplication, QEvent
    QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
    QTest.qWait(10)
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
    source = re.sub(r"\s+(/?>)", r"\1", source)
    return source.strip()


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
ET.fromstring(compacted)  # A `<path ... />` must not turn into an unclosed `<path>`.
assert '<path' in compacted and '/>' in compacted, "self-closing drawing elements must survive cleanup"
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


# A large SVG stays editable in the profile. Referenced PNG copies keep the
# About document bounded even with several icons, without relying on the
# player's SVG decoder. Check both the over-budget input and the short-URL form.
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
# Runtime files replace the entire numeric map, rather than adding filename
# keys beside data URIs that the delegate would continue reading.
assert "runtimeAssetPng" in cpp and "byFile = byTabIndex(byFile, current.tabs)" in cpp
assert "return rasterizeSvg(bytes, icon ? 54 : 320, icon);" in cpp, \
    "runtime copies must render icons at sufficient resolution while preserving the saved SVG"
file_form = "({" + ",".join(
    f'"{i}":"file:///C:/Users/Test/AppData/Local/BSI/settings-panel-assets/'
    + "a" * 64 + "/" + __import__("hashlib").sha256(str(i).encode()).hexdigest()[:24]
    + '.png"' for i in range(4)) + "})"
assert _page_payload(file_form) < 2337, "file references must leave the About page inside the known slot"
print("PASS: oversized icon artwork can use compact numeric file references without altering the SVG profile")
