"""Production source planner + plugin + native Settings callbacks in Qt fixtures.

Not a running emulator: native proprietary C++ model/controls/backend types are
contract fixtures; the root bootstrap, window source/callbacks, appended row,
shader module and shader controls are production code. Run missing-module mode
in a separate process so a previously registered plugin cannot mask a failure.
"""
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import uuid

# This imports one Qt binding and applies the same runtime/style choices as the
# existing real plugin test. It does not import/register the shader QML module.
from shaders_plugin_ui_test import (
    QCoreApplication, QFileInfo, QGuiApplication, QLibraryInfo, QObject, QPointF,
    QQmlComponent, QQmlEngine, QQmlExpression, QStandardPaths, Qt, QTest, QUrl,
    configure_host_qt,
)
if os.environ.get("BSI_TEST_QT_API") == "PyQt5":
    from PyQt5.QtCore import pyqtProperty as Property, pyqtSlot as Slot
else:
    from PySide6.QtCore import Property, Slot

REPO = Path(__file__).resolve().parents[1]


class Backend(QObject):
    def __init__(self, instance):
        super().__init__()
        self.instance = instance
        self.shortcuts = True
        self.discards = 0

    @Slot(str, result=bool)
    def confGetBool(self, _key):
        return False

    @Slot(result=bool)
    def confIsModified(self):
        return False

    @Slot(str, result=str)
    def confGetInstanceString(self, key):
        assert key == "graphics_renderer"
        return "gl"

    @Slot(result=str)
    def getVmName(self):
        return self.instance

    @Slot()
    def confDiscard(self):
        self.discards += 1

    @Slot()
    def setSettingsWindowObject(self):
        pass

    @Slot()
    def clearSettingsWindowObject(self):
        pass

    @Slot(bool)
    def shortcutsEditingMode(self, _enable):
        pass

    @Property(bool)
    def pIsShortcutsEnabled(self):
        return self.shortcuts

    @pIsShortcutsEnabled.setter
    def pIsShortcutsEnabled(self, value):
        self.shortcuts = value


def fixtures(directory):
    module = directory / "BsiNativeFixture"
    module.mkdir()
    declarations = {
        "SettingElementModel": '''import QtQml 2.15
QtObject {
 property string pButtonText: ""
 property string pButtonId: ""
 property url pQmlSource: ""
 property var pComponentArgs: null
 property QtObject pComponentObj: null
 property bool pIsVisible: true
 property bool pShowRedDot: false
}
''',
        "SettingElementModelList": '''import QtQml 2.15
QtObject { default property list<QtObject> pSettingElements }
''',
        "DialogButtonModel": '''import QtQuick 2.15
QtObject { property string pText: ""; property color pColor: "transparent"; signal clicked() }
''',
        "DialogButtonModelList": '''import QtQml 2.15
QtObject { property list<QtObject> pButtons }
''',
        "UiDialogWindow": '''import QtQml 2.15
QtObject { enum Kind { Simple, SimpleNarrow, Tall } }
''',
        "UiMessageDialog": '''import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
Window {
 id:dialog;objectName:"nativeDiscardPrompt";width:390;height:190
 // Match the native dialog's modal/transient and JS-owned close lifecycle.
 flags:Qt.Dialog|Qt.FramelessWindowHint
 modality:Qt.ApplicationModal
 onVisibleChanged:if(!visible)dialog.destroy()
 property int pDialogType:0
 property string pDescription:""
 property var pButtonsList:null
 Text {text:dialog.pDescription;width:370;wrapMode:Text.Wrap;x:10;y:15}
 Row {x:10;y:120;spacing:10
 Repeater {model:dialog.pButtonsList?dialog.pButtonsList.pButtons:[]
 delegate:Button {objectName:"nativePromptButton"+index;text:modelData.pText;onClicked:modelData.clicked()}}}
}
''',
        "UiShadowContainer": '''import QtQuick 2.15
Item {
 default property alias children:content.data
 property real shadowThickness:15
 Item {id:content;anchors.fill:parent;anchors.margins:shadowThickness}
}
''',
        "UiImageButton": '''import QtQuick 2.15
Item {
 property string asset:""
 objectName:asset
 signal clicked()
 width:24;height:24
 MouseArea{anchors.fill:parent;onClicked:parent.clicked()}
}
''',
        "UiToastPopup": '''import QtQml 2.15
QtObject {
 property string pToastMessage:""
 function fShowMessage(_value){}
 function fClosePopup(){}
}
''',
    }
    theme = '''pragma Singleton
import QtQuick 2.15
QtObject {
 property QtObject colors:QtObject{
 property color primary10:"#f4f7fb"
 property color primary20:"#d2dae4"
 property color primary30:"#a9adba"
 property color primary60:"#35465a"
 property color primary80:"#101a27"
 property color accentNormal:"#54c8ff"
 property color subWindowHeaderClick:"#54c8ff"
 property color dangerNormal:"#ee3344"
 }
 property QtObject fonts:QtObject{
 property font bodyLarge:Qt.font({pixelSize:17})
 property font bodyMedium:Qt.font({pixelSize:14})
 property font bodySmall:Qt.font({pixelSize:12})
 }
 property QtObject buttons:QtObject{property QtObject colors:QtObject{
 property color danger:"#ee3344"
 property color secondary:"#35465a"
 }}
}
'''
    qmldir = "module BsiNativeFixture\nsingleton UiTheme 1.0 UiTheme.qml\n"
    for name, source in declarations.items():
        (module / (name + ".qml")).write_text(source)
        qmldir += f"{name} 1.0 {name}.qml\n"
    (module / "UiTheme.qml").write_text(theme)
    (module / "qmldir").write_text(qmldir)
    # URLs are case sensitive on Linux; use the planner fixture's actual native
    # spellings, not the artwork catalog's many historical filename aliases.
    pages = re.findall(r'QStringLiteral\("([A-Za-z]+Setting.qml)"\)', (REPO / "tests/shaders/settings_fixture.h").read_text())
    assert len(pages) == 12
    for name in pages:
        (directory / name).write_text('''import QtQuick 2.15
Item {
 property string pParentWindow:""
 property bool pEnableSave:false
 function fOnQmlLoaded(){}
 function fDiscardChanges(){}
 Text {text:"Untouched native-page fixture"}
}
''')
    # Use the complete generated About/material alongside the actual native
    # shell and public plugin. Standalone shader controls cannot catch styling
    # that accidentally rewrites a native indicator/background or model index.
    cpp = (REPO / "src/core/SettingsPanelPatcher.cpp").read_text()
    about = re.search(r'QString generated = QStringLiteral\(R"QML\((.*?)\)QML"\);', cpp, re.S)[1]
    for key, value in {"DEVELOPER": json.dumps("Native Settings contract"),
                       "TAGLINE": json.dumps("Not an installed emulator"),
                       "TAB_ICONS": "({})", "LINKS": "", "LOGO_SOURCE": json.dumps("")}.items():
        about = about.replace("@@BSI_" + key + "@@", value)
    assert "@@BSI_" not in about
    about = about.replace("import QtQuick.Controls 2.15", "import QtQuick.Controls 2.15\nimport BsiNativeFixture 1.0")
    (directory / "PlrAboutSetting.qml").write_text(about)


def main():
    native_diagnostics = None
    if sys.platform == "win32" and os.environ.get("BSI_SHADERS_DIAGNOSTICS"):
        from shaders.windows_diagnostics import install
        native_diagnostics = install()
    missing = "--missing-module" in sys.argv
    planner = Path(os.environ["BSI_SHADERS_SOURCE_TEST"]).resolve()
    module_root = Path(os.environ["BSI_SHADERS_MODULE_ROOT"]).resolve()
    assert planner.is_file()
    runtime_qml = configure_host_qt()
    QStandardPaths.setTestModeEnabled(True)
    app = QGuiApplication.instance() or QGuiApplication([])
    with tempfile.TemporaryDirectory(prefix="bsi-native-shaders-") as temp, ExitStack() as cleanup:
        directory = Path(temp)
        fixtures(directory)
        # Assemble the complete approved shell from the measured native source,
        # not a fake centered tab strip. This import also checks its slot budget.
        from settings_panel_window_budget_test import compact
        stock = re.sub(r'^import (?:DialogButtonModel|DialogButtonModelList|UiDialogWindow|SettingElementModel|SettingElementModelList) 1\.0\r?\n', "", compact, flags=re.M)
        stock = "import BsiNativeFixture 1.0\n" + stock
        source_path = directory / "NativeSettingsBase.qml"
        source_path.write_text(stock)
        subprocess.run([str(planner), "--emit", str(directory), str(source_path)], check=True)
        engine = QQmlEngine()
        engine.setImportPathList(([str(module_root)] if not missing else []) + [str(directory), runtime_qml, "qrc:/qt-project.org/imports", "qrc:/qt/qml"])
        warnings = []
        engine.warnings.connect(lambda errors: warnings.extend(e.toString() for e in errors))
        backend = Backend("NativeFixture-" + uuid.uuid4().hex)
        engine.rootContext().setContextProperty("uiBackend", backend)
        engine.rootContext().setContextProperty("plrBackend", backend)
        executable = QFileInfo(QCoreApplication.applicationFilePath()).canonicalFilePath()
        key = hashlib.sha256((executable + "\n" + backend.instance).encode()).hexdigest()
        profile = Path(QStandardPaths.writableLocation(QStandardPaths.StandardLocation.GenericConfigLocation)) / "BSICustomizer/Shaders" / (key + ".json")
        assert not profile.exists() and not profile.is_symlink()
        cleanup.callback(lambda: profile.unlink(missing_ok=True))
        component = QQmlComponent(engine, QUrl.fromLocalFile(str(directory / "Player.qml")))
        assert component.status() == QQmlComponent.Status.Ready, [e.toString() for e in component.errors()]
        player = component.create()
        assert player, [e.toString() for e in component.errors()]
        QTest.qWait(80)
        assert player.property("pCompLoaded"), "optional shader bootstrap interrupted native startup"
        assert player.property("bsiShaderReady") is (not missing), warnings

        def evaluate(context, expression):
            expr = QQmlExpression(QQmlEngine.contextForObject(context) or engine.rootContext(), context, expression)
            result = expr.evaluate()
            assert not expr.hasError(), expr.error().toString()
            return result[0] if isinstance(result, tuple) else result

        def tree(item):
            pending = list(item.childItems())
            while pending:
                child = pending.pop()
                yield child
                pending.extend(child.childItems())

        def named(root, name):
            return next(item for item in tree(root) if item.objectName() == name)

        def click(window, control):
            # A Quick item stays isVisible() even when a focused ScrollView has
            # scrolled it behind a clipped header/footer (notably Qt 5 Windows).
            # Reveal it using the real Flickable, then reject any clipped hit.
            ancestor = control.parentItem()
            while ancestor:
                if ancestor.metaObject().indexOfProperty("contentY") >= 0:
                    center = control.mapToItem(ancestor, QPointF(control.width() / 2, control.height() / 2))
                    margin = min(control.height() / 2 + 2, ancestor.height() / 2)
                    offset = min(center.y() - margin, 0) + max(center.y() - ancestor.height() + margin, 0)
                    maximum = max(0, ancestor.property("contentHeight") - ancestor.height())
                    ancestor.setProperty("contentY", max(0, min(maximum, ancestor.property("contentY") + offset)))
                    QTest.qWait(20)
                ancestor = ancestor.parentItem()
            ancestor = control.parentItem()
            while ancestor:
                if ancestor.clip():
                    center = control.mapToItem(ancestor, QPointF(control.width() / 2, control.height() / 2))
                    assert 0 <= center.x() < ancestor.width() and 0 <= center.y() < ancestor.height(), f"physical target {name_of(control)} is clipped by {name_of(ancestor)}"
                ancestor = ancestor.parentItem()
            point = control.mapToScene(QPointF(control.width() / 2, control.height() / 2)).toPoint()
            assert control.isVisible() and 0 <= point.x() < window.width() and 0 <= point.y() < window.height(), name_of(control)
            QTest.mouseMove(window, point)
            QTest.qWait(20)
            QTest.mouseClick(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, point)
            QTest.qWait(40)

        def name_of(control):
            return control.objectName() or control.metaObject().className()

        def open_settings():
            click(player, named(player.contentItem(), "nativeSettingsButton"))
            window = player.property("pSettingsWindow")
            assert window and window.isVisible(), warnings
            assert (window.width(), window.height()) == (780, 500), "native geometry/gutters regressed"
            assert evaluate(window, "pSettingsItems.pSettingElements.length") == 13
            return window

        def tab(window, index, pointer_scroll=False):
            strip = next(i for i in tree(window.contentItem()) if i.metaObject().className().endswith("ListView"))

            def settled():
                # A ListView may already report atXEnd while its overshoot or
                # kinetic animation is still running (Qt 6 Windows). A real
                # click during that motion only stops the flick and must not
                # be mistaken for a category activation. Wait for ordinary
                # pointer-driven scrolling to finish; never cancel/position it.
                for _ in range(150):
                    if not any(strip.property(p) for p in ("moving", "flicking", "dragging")):
                        return
                    QTest.qWait(20)
                assert False, "native top-strip scrolling did not settle after a real pointer drag"

            if pointer_scroll:
                # Prove the appended category is reachable with an ordinary
                # mouse, not only positionViewAtIndex/contentX assignment. Drag
                # the native horizontal ListView left to expose its right end.
                for _ in range(8):
                    if strip.property("atXEnd"):
                        break
                    start = strip.mapToScene(QPointF(strip.width() * .85, strip.height() / 2)).toPoint()
                    end = strip.mapToScene(QPointF(strip.width() * .15, strip.height() / 2)).toPoint()
                    QTest.mouseMove(window, start)
                    QTest.mousePress(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, start)
                    for step in range(1, 13):
                        point = start + (end - start) * (step / 12)
                        QTest.mouseMove(window, point, 15)
                    QTest.mouseRelease(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, end)
                    QTest.qWait(20)
                    settled()
                assert strip.property("atXEnd"), "the native top strip cannot be scrolled to Shaders with a real mouse drag"
            else:
                strip.setProperty("cacheBuffer", 10000)
                evaluate(strip, f"positionViewAtIndex({index}, ListView.Contain)")
            QTest.qWait(80)
            settled()
            text = evaluate(window, f"pSettingsItems.pSettingElements[{index}].pButtonText")
            label = next(i for i in tree(window.contentItem()) if i.metaObject().indexOfProperty("text") >= 0 and i.property("text") == text)
            click(window, label.parentItem())
            return {"requested": index, "selected": window.property("pCurrentTabIndex"),
                    "contentX": strip.property("contentX"), "contentWidth": strip.property("contentWidth"),
                    "atXEnd": strip.property("atXEnd"), "moving": strip.property("moving"),
                    "flicking": strip.property("flicking"), "dragging": strip.property("dragging"),
                    "rowVisible": evaluate(window, f"pSettingsItems.pSettingElements[{index}].pIsVisible"),
                    "hit": (label.mapToScene(QPointF(label.width() / 2, label.height() / 2)).x(),
                            label.mapToScene(QPointF(label.width() / 2, label.height() / 2)).y()),
                    "warnings": warnings}

        def visible_prompts():
            return [w for w in QGuiApplication.topLevelWindows() if w.objectName() == "nativeDiscardPrompt" and w.isVisible()]

        def prompt_button(text):
            # The Windows Qt 5 platform can queue modal/transient changes. Wait
            # for the actual window, never invoke a mocked handler or turn a
            # missing prompt into a pass. Keep diagnostics useful on CI.
            for _ in range(50):
                if visible_prompts():
                    break
                QTest.qWait(20)
            prompts = visible_prompts()
            assert len(prompts) == 1, (
                "native dirty-state prompt not shown exactly once: "
                + repr([(w.objectName(), w.isVisible(), w.property("bsiShaderDirty2"))
                        for w in QGuiApplication.topLevelWindows()])
                + f"; QML warnings: {warnings}"
            )
            prompt = prompts[0]
            button = next(i for i in tree(prompt.contentItem()) if i.objectName().startswith("nativePromptButton") and i.property("text") == text)
            click(prompt, button)
            assert not visible_prompts(), "native prompt remained visible after its physical button click"

        window = open_settings()
        if missing:
            # The row's placeholder object keeps unrelated native callback loops
            # safe even though no shader page is loaded from the absent module.
            for callback in ("fRefreshAudioSettingModels(false)", "fPreferenceSettingChangedClbk(false)", "fAdvanceSettingChangedClbk(false)", "fReloadSettingComboboxes()"):
                evaluate(window, callback)
            assert not evaluate(window, "pSettingsItems.pSettingElements[12].pIsVisible")
            assert not window.property("bsiShaderDirty2")
            assert not any(i.objectName() == "shaderEnabled" for i in tree(window.contentItem()))
            window.close()
            QTest.qWait(60)
            assert player.property("pSettingsWindow") is None
            assert not profile.exists()
        else:
            navigation = tab(window, 12, pointer_scroll=True)
            assert window.property("pCurrentTabIndex") == 12, f"physical Shaders activation failed after pointer scrolling: {navigation}"
            page = evaluate(window, "pSettingsItems.pSettingElements[12].pComponentObj")
            assert page and evaluate(page, "Engine.controls.length") == 10
            assert page.property("tabsArranged"), "Shaders must sort visually before About"
            assert page.property("bsiTabIndex") == 12, "visual sorting changed the native callback index"
            assert evaluate(window, "String(pSettingsItems.pSettingElements[11].pQmlSource).endsWith('PlrAboutSetting.qml')")
            strip = next(i for i in tree(window.contentItem()) if i.metaObject().className().endswith("ListView"))
            shader_label = next(i for i in tree(strip) if i.metaObject().indexOfProperty("text") >= 0 and i.property("text") == "Shaders")
            about_label = next(i for i in tree(strip) if i.metaObject().indexOfProperty("text") >= 0 and i.property("text") == "About")
            assert shader_label.mapToItem(strip, QPointF(0, 0)).x() < about_label.mapToItem(strip, QPointF(0, 0)).x(), "Shaders is not visually before About"
            about = evaluate(window, "pSettingsItems.pSettingElements[11].pComponentObj")
            assert about.objectName() == "aboutSettings" and evaluate(page, "bsiTheme !== null")
            # The generated material must preserve the shader controls' own
            # surfaces and live native palette, not substitute a fixed theme.
            combo = named(page, "shaderPresets")
            slider = named(page, "shaderValue-saturation")
            check = named(page, "shaderEnabled")
            assert evaluate(combo, "background.gradient.stops.length") == 2
            assert evaluate(slider, "handle.gradient.stops.length") == 2
            assert evaluate(check, "indicator.radius") == 2
            evaluate(about, "UiTheme.colors.accentNormal = '#ab5544'")
            assert evaluate(combo, "String(shaderStyle.accent)") == "#ab5544"
            evaluate(about, "UiTheme.colors.accentNormal = '#54c8ff'")
            QTest.qWait(50)
            assert evaluate(page, "viewport.contentHeight === Math.ceil(form.implicitHeight) + 20"), "native themed scroll extent did not settle"
            assert not window.property("bsiShaderDirty2")
            initial = evaluate(page, "JSON.stringify(Engine.saved)")
            click(window, named(page, "shaderEnabled"))
            assert window.property("bsiShaderDirty2")
            tab(window, 0)
            assert window.property("pCurrentTabIndex") == 12, "native navigation silently lost unsaved shader edits"
            prompt_button("No")
            assert window.property("pCurrentTabIndex") == 12 and page.property("pEnableSave")
            tab(window, 0)
            prompt_button("Discard Changes")
            assert window.property("pCurrentTabIndex") == 0 and not window.property("bsiShaderDirty2")
            assert evaluate(page, "JSON.stringify(Engine.saved)") == initial
            tab(window, 12)
            click(window, named(page, "shaderEnabled"))
            # Saved values, not draft bindings, drive the real C++ bridge.
            click(window, named(page, "shaderSave"))
            saved = evaluate(page, "JSON.stringify(Engine.saved)")
            assert json.loads(saved)["enabled"] and profile.is_file()
            assert not window.property("bsiShaderDirty2")
            click(window, named(page, "shaderEnabled"))
            assert page.property("pEnableSave") and window.property("bsiShaderDirty2"), (
                "physical post-Save edit did not publish native dirty state: "
                + repr({"checked": named(page, "shaderEnabled").property("checked"),
                        "controlEnabled": named(page, "shaderEnabled").property("enabled"),
                        "draft": evaluate(page, "JSON.stringify(draft)"),
                        "saved": evaluate(page, "JSON.stringify(Engine.saved)"),
                        "pageDirty": page.property("pEnableSave"),
                        "nativeDirty": window.property("bsiShaderDirty2"), "warnings": warnings})
            )
            # Use the actual native header close action, not a Python wrapper
            # call, and require both No and Discard to traverse onClosing.
            click(window, named(window.contentItem(), "CloseIcon"))
            prompt_button("No")
            assert window.isVisible() and window.property("bsiShaderDirty2")
            click(window, named(window.contentItem(), "CloseIcon"))
            prompt_button("Discard Changes")
            QTest.qWait(60)
            assert player.property("pSettingsWindow") is None, "native close/discard recursed or left a dirty window alive"
            assert backend.discards == 1
            window = open_settings()
            tab(window, 12)
            page = evaluate(window, "pSettingsItems.pSettingElements[12].pComponentObj")
            assert evaluate(page, "JSON.stringify(Engine.saved)") == saved
            assert not page.property("pEnableSave") and not window.property("bsiShaderDirty2")
            evaluate(page, "reset()")
            click(window, named(page, "shaderSave"))
            assert evaluate(page, "JSON.stringify(Engine.saved)") == initial
            window.close()
            QTest.qWait(60)
        assert not warnings, "QML runtime warnings:\n" + "\n".join(warnings)
        player.close()
        player.deleteLater()
        QTest.qWait(30)
        engine.deleteLater()
        app.processEvents()
    print("PASS: " + ("missing regular DLL does not block native startup; shader row/page are inactive, all native callback loops and Settings close remain intact" if missing else "production optional bootstrap imports the real plugin with complete generated About glass; Shaders sorts before About without changing native indexes, retains themed controls, mouse-drag navigation across 13 tabs, native No/Discard on tab and close, Save, persistence/reopen, reset and intact 750x470 layout"))
    print("LIMIT: Qt/native-contract fixture only; no installed emulator, real game frames, deployment recovery or physical Intel/AMD/NVIDIA coverage is implied")


if __name__ == "__main__":
    main()
