"""Real native shader-plugin/component tests, not an emulator certification.

BSI_SHADERS_MODULE_ROOT must point to a compiled BSIShaders module directory's
parent. This loads the actual C++ plugin, not a mocked ShaderBridge. The host
window is an explicit Qt fixture; no claim of game-renderer integration follows.
Run Qt 5 and Qt 6 in separate processes with BSI_TEST_QT_API=PyQt5/PySide6.
"""
from contextlib import ExitStack
import hashlib
import json
import math
import os
from pathlib import Path
import sys
import tempfile
import uuid

if os.environ.get("BSI_SHADERS_DIAGNOSTICS"):
    import faulthandler
    faulthandler.enable()
    # Preserve where a native Qt host stalls, including imports/shutdown. CI
    # bounds the command separately and still counts a hang as a failure.
    faulthandler.dump_traceback_later(60, repeat=True)

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QUICK_BACKEND", "software")
# Basic was introduced in Qt 6; Qt 5 calls its base style Default. Keep the
# host's SDK/QML paths separate from the Python runtime's own QML plugins below.
os.environ.setdefault("QT_QUICK_CONTROLS_STYLE", "Default" if os.environ.get("BSI_TEST_QT_API") == "PyQt5" else "Basic")
if os.environ.get("BSI_TEST_QT_API") == "PyQt5":
    from PyQt5.QtCore import pyqtProperty as Property, pyqtSignal as Signal, pyqtSlot as Slot
    from PyQt5.QtCore import QCoreApplication, QFileInfo, QLibraryInfo, QObject, QPointF, QRectF, QStandardPaths, Qt, QUrl, qVersion
    from PyQt5.QtGui import QGuiApplication
    from PyQt5.QtQml import QQmlComponent, QQmlEngine, QQmlExpression
    from PyQt5.QtQuick import QQuickItem, QQuickWindow
    from PyQt5.QtTest import QTest
else:
    from PySide6.QtCore import Property, Signal, Slot
    from PySide6.QtCore import QCoreApplication, QFileInfo, QLibraryInfo, QObject, QPointF, QRectF, QStandardPaths, Qt, QUrl, qVersion
    from PySide6.QtGui import QGuiApplication
    from PySide6.QtQml import QQmlComponent, QQmlEngine, QQmlExpression
    from PySide6.QtQuick import QQuickItem, QQuickWindow
    from PySide6.QtTest import QTest


class NativeDisplayFixture(QObject):
    """Authored native configuration contract, NOT a real guest or restart.

    The real C++ module must invoke these typed public Qt methods. No QML mock
    replaces Engine, preferences, native-method adapter or file transaction.
    """
    flickableMovingChanged = Signal()
    pIsEditingTextfieldChanged = Signal()
    shortcutsChanged = Signal()

    @Property(str, constant=True)
    def productName(self): return "Native contract player"

    @Property(bool, notify=shortcutsChanged)
    def pIsShortcutsEnabled(self): return self._shortcuts

    @pIsShortcutsEnabled.setter
    def pIsShortcutsEnabled(self, value):
        if self._shortcuts != value:
            self._shortcuts = value
            self.shortcutsChanged.emit()

    def __init__(self):
        super().__init__()
        self._moving, self._editing, self._shortcuts = False, False, True
        self.committed = {"fb_width": 1920, "fb_height": 1080, "custom_resolution_selected": False,
                          "dpi": 320, "fps": 90, "renderer": "vlcn", "unrelated": 42}
        self.draft = dict(self.committed)
        self.commits = 0
        self.discards = 0
        self.writes = []
        self.restarts = 0
        self.reject_restart = False

    @Property(bool, notify=flickableMovingChanged)
    def pFlickableMoving(self): return self._moving

    @pFlickableMoving.setter
    def pFlickableMoving(self, value):
        if self._moving != value:
            self._moving = value
            self.flickableMovingChanged.emit()

    @Property(bool, notify=pIsEditingTextfieldChanged)
    def pIsEditingTextfield(self): return self._editing

    @pIsEditingTextfield.setter
    def pIsEditingTextfield(self, value):
        if self._editing != value:
            self._editing = value
            self.pIsEditingTextfieldChanged.emit()

    @Slot(str, result=str)
    def confGetString(self, key):
        assert key == "bst.ui_scale_factor", key
        return "1.0"

    @Slot(str, result=int)
    def confGetInstanceI32(self, key):
        assert key in ("fb_width", "fb_height"), key
        return self.draft[key]

    @Slot(str, result=bool)
    def confGetInstanceBool(self, key):
        assert key == "custom_resolution_selected", key
        return self.draft[key]

    @Slot(result=bool)
    def confIsModified(self):
        return self.draft != self.committed

    @Slot(str, int)
    def confSetInstanceI32(self, key, value):
        assert key in ("fb_width", "fb_height"), key
        self.writes.append((key, value))
        self.draft[key] = value

    @Slot(str, bool)
    def confSetInstanceBool(self, key, value):
        assert key == "custom_resolution_selected", key
        self.writes.append((key, value))
        self.draft[key] = value

    @Slot()
    def confCommit(self):
        self.committed = dict(self.draft)
        self.commits += 1

    @Slot(result=bool)
    def restartPlayer(self):
        self.restarts += 1
        return not self.reject_restart

    @Slot()
    def confDiscard(self):
        self.draft = dict(self.committed)
        self.discards += 1


_host_dll_directories = []


def configure_host_qt():
    """Keep SDK QPA/private plugins out of the independent public-bridge host."""
    if os.environ.get("BSI_TEST_QT_API") == "PyQt5":
        plugins = QLibraryInfo.location(QLibraryInfo.PluginsPath)
        qml = QLibraryInfo.location(QLibraryInfo.Qml2ImportsPath)
        binaries = QLibraryInfo.location(QLibraryInfo.BinariesPath)
    else:
        plugins = QLibraryInfo.path(QLibraryInfo.LibraryPath.PluginsPath)
        qml = QLibraryInfo.path(QLibraryInfo.LibraryPath.QmlImportsPath)
        binaries = QLibraryInfo.path(QLibraryInfo.LibraryPath.BinariesPath)
    if sys.platform == "win32":
        from shaders.qt_host_paths import windows_host_binaries
        binding_root = Path(sys.modules[QCoreApplication.__module__].__file__).resolve().parent
        binaries = str(windows_host_binaries(binding_root, binaries, int(qVersion().split(".")[0])))
        os.environ["PATH"] = binaries + os.pathsep + os.environ.get("PATH", "")
        _host_dll_directories.append(os.add_dll_directory(binaries))
        print("Independent host DLL directory:", binaries, flush=True)
    assert Path(plugins).is_dir() and Path(qml).is_dir(), (plugins, qml)
    # install-qt-action exports paths for BUILDING. QPA is private/version-bound
    # and must come from the Python HOST, before constructing QGuiApplication.
    # The BSI bridge itself remains the genuinely older compiled public plugin.
    os.environ["QT_PLUGIN_PATH"] = plugins
    os.environ["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(Path(plugins) / "platforms")
    os.environ.pop("QML2_IMPORT_PATH", None)
    os.environ.pop("QML_IMPORT_PATH", None)
    QCoreApplication.setLibraryPaths([plugins])
    print("Independent host Qt:", qVersion(), "native plugins:", plugins, flush=True)
    return qml


def main():
    native_diagnostics = None
    if sys.platform == "win32" and os.environ.get("BSI_SHADERS_DIAGNOSTICS"):
        from shaders.windows_diagnostics import install
        native_diagnostics = install()
    module_root = Path(os.environ["BSI_SHADERS_MODULE_ROOT"]).resolve()
    assert (module_root / "BSIShaders/qmldir").is_file()
    runtime_qml = configure_host_qt()
    with tempfile.TemporaryDirectory(prefix="bsi-shader-settings-") as temp, ExitStack() as cleanup:
        os.environ["XDG_CONFIG_HOME"] = temp
        # Do not assume Windows Known Folder APIs honor APPDATA env overrides.
        # Use Qt's real test-mode paths, a unique instance, and delete only the
        # exact new profile that this test owns, even when an assertion fails.
        QStandardPaths.setTestModeEnabled(True)
        app = QGuiApplication.instance() or QGuiApplication([])
        if os.environ.get("BSI_SHADERS_TEST_FONTLESS") != "1":
            from shaders.qt_fonts import install_readable_test_font
            install_readable_test_font(app)
        instance = "Fixture-" + uuid.uuid4().hex
        executable = QFileInfo(QCoreApplication.applicationFilePath()).canonicalFilePath()
        key = hashlib.sha256((executable + "\n" + instance).encode()).hexdigest()
        profile = Path(QStandardPaths.writableLocation(QStandardPaths.StandardLocation.GenericConfigLocation)) / "BSICustomizer/Shaders" / (key + ".json")
        assert not profile.exists() and not profile.is_symlink(), "test refuses to overwrite any existing profile"
        cleanup.callback(lambda: profile.unlink(missing_ok=True))
        engine = QQmlEngine()
        from shaders.native_ui_fixture import install as install_native_ui, write_shell_material
        write_shell_material(Path(temp))
        native_theme, unregister_native_ui = install_native_ui(engine, Path(temp))
        cleanup.callback(unregister_native_ui)
        native_display = NativeDisplayFixture()
        engine.rootContext().setContextProperty("uiBackend", native_display)
        engine.rootContext().setContextProperty("plrBackend", native_display)
        # install-qt-action supplies QML2_IMPORT_PATH for the C++ build SDK. The
        # Python host is a separate, ABI-compatible Qt distribution (not always
        # the identical patch release). Letting both register Controls loads the
        # plugin twice under different paths and Qt 5 rejects the second as a
        # protected module. Never mix their private QML plugins. This changes
        # only the fixture's search path, not the plugin or a player's engine.
        assert Path(runtime_qml).is_dir(), runtime_qml
        engine.setImportPathList([str(module_root), runtime_qml, "qrc:/qt-project.org/imports", "qrc:/qt/qml"])
        print("Fixture Qt runtime:", qVersion(), "QML paths:", engine.importPathList(), flush=True)
        warnings = []
        engine.warnings.connect(lambda errors: warnings.extend(error.toString() for error in errors))
        # Check the host's real Controls installation independently, before
        # importing BSIShaders, so host packaging errors cannot look like a
        # shader success/failure. Generic Qt Controls are never mocked; the
        # explicit native-QRC adapter reports real resources versus contracts.
        controls = QQmlComponent(engine)
        controls.setData(b'import QtQuick 2.15; import QtQuick.Controls 2.15; Button { text: "Controls preflight" }', QUrl("file:///ControlsPreflight.qml"))
        assert controls.status() == QQmlComponent.Status.Ready, [e.toString() for e in controls.errors()]
        control = controls.create()
        assert control, [e.toString() for e in controls.errors()]
        control.deleteLater()
        app.processEvents()
        qml = b'''import QtQuick 2.15
import QtQuick.Window 2.15
import DialogButtonModel 1.0
import DialogButtonModelList 1.0
import UiDialogWindow 1.0
import BSIShaders 1.0
import "qrc:/" as Native
Window {
    id: host
    width: 750; height: 470; visible: true
    color: "#ff00ff"
    property alias shaderPage: shaderLoader.item
    property int pCurrentTabIndex: 0
    property bool bsiShaderDirty: false
    property int restartLoaderCount: 0
    function showRestartLoader() { ++restartLoaderCount }
    Component {
        id: stockRestartReference
        Native.UiMessageDialog {
            id: reference
            objectName: "stockRestartReference"
            title: qsTranslate("QObject", "Restart %1").arg(uiBackend.productName)
            pDescription: qsTranslate("QObject", "Some of the changes need %1 to restart. Do you want to restart now?").arg(uiBackend.productName)
            pDialogType: UiDialogWindow.Simple
            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel { pText: "Restart Now"; pColor: UiTheme.buttons.colors.accent; onClicked: reference.close() },
                    DialogButtonModel { pText: "Discard Changes"; pColor: UiTheme.buttons.colors.secondary; onClicked: reference.close() }
                ]
            }
        }
    }
    function openRestartReference() { stockRestartReference.createObject(host, {transientParent: host}).show() }
    // Same native factory/roles as Display/Graphics, not the shader page's
    // implementation. A custom-looking replacement cannot satisfy identity.
    Native.UiButton {
        id: nativeSaveControl
        objectName: "stockSaveReference"
        visible: false
        pSize: UiTheme.buttons.sizes.medium
        pColor: UiTheme.buttons.colors.accent
        text: qsTranslate("QObject", "Save changes")
    }
    Bootstrap { hostWindow: host; instanceName: "Fixture-Nougat64"; graphicsRenderer: "vlcn" }
    Item {
        anchors.fill: parent
        property var bIcons: ({})
        Loader {
            id: shaderLoader
            anchors.fill: parent; anchors.margins: 12
            source: "qrc:/bsi-shaders/BsiShadersSetting.qml"
            onLoaded: { item.bsiTabIndex = 12; item.fOnQmlLoaded() }
        }
        // Native Settings eagerly loads About even while it is not selected.
        // Exercise the shipping material traversal against ACTUAL native
        // controls; the independent Save reference above is outside this scope.
        Loader {
            id: materialLoader
            objectName: "shaderShellMaterial"
            anchors.fill: parent
            source: "ShellMaterial.qml"
            visible: false
            onLoaded: item.fOnQmlLoaded()
        }
    }
    function savedSettings() { return JSON.stringify(Engine.saved) }
}
'''
        qml = qml.replace(b"Fixture-Nougat64", instance.encode())
        component = QQmlComponent(engine)
        component.setData(qml, QUrl.fromLocalFile(str(Path(temp) / "ShaderHost.qml")))
        assert component.status() == QQmlComponent.Status.Ready, [e.toString() for e in component.errors()]
        window = component.create()
        assert window, [e.toString() for e in component.errors()]
        # The fixture explicitly owns/deletes its host, rather than inferring
        # lifetime from temporary QML/Python wrappers of JS-owned dialogs.
        QQmlEngine.setObjectOwnership(window, QQmlEngine.ObjectOwnership.CppOwnership)
        QObject.setParent(window, engine)
        QTest.qWait(220)  # include a full production About-material timer pass
        page = window.property("shaderPage")
        assert page, warnings

        def evaluate(expression, context=page):
            expr = QQmlExpression(QQmlEngine.contextForObject(context) or engine.rootContext(), context, expression)
            result = expr.evaluate()
            assert not expr.hasError(), expr.error().toString()
            # PyQt returns (value, undefined); PySide returns a plain value.
            if isinstance(result, tuple):
                result = result[0]
            return result

        def settings():
            return json.loads(evaluate("savedSettings()", window))

        def all_items(root=None):
            # Repeater delegates follow the visual, not QObject ownership tree.
            pending = [root or page]
            while pending:
                child = pending.pop()
                yield child
                pending.extend(child.childItems())

        def item(name):
            return next(child for child in all_items() if child.objectName() == name)

        def inside_clip(control, ancestor):
            bounds = control.mapRectToItem(ancestor, QRectF(0, 0, control.width(), control.height()))
            return (bounds.left() >= -.5 and bounds.top() >= -.5
                    and bounds.right() <= ancestor.width() + .5
                    and bounds.bottom() <= ancestor.height() + .5)

        def click(control, x_ratio=.5, target_window=None):
            target_window = target_window or window
            # Settle the preceding layout/focus change, reveal the target ONCE,
            # then require the entire control to fit before a real pointer hit.
            # Vertical scrolling cannot repair an oversized horizontal layout.
            # No repeated reveal, click retry or property-only action fallback.
            QTest.qWait(30)
            ancestor = control.parentItem()
            while ancestor:
                if ancestor.metaObject().indexOfProperty("contentY") >= 0:
                    center = control.mapToItem(ancestor, QPointF(control.width() / 2, control.height() / 2))
                    margin = min(control.height() / 2 + 2, ancestor.height() / 2)
                    offset = min(center.y() - margin, 0) + max(center.y() - ancestor.height() + margin, 0)
                    maximum = max(0, ancestor.property("contentHeight") - ancestor.height())
                    ancestor.setProperty("contentY", max(0, min(maximum, ancestor.property("contentY") + offset)))
                ancestor = ancestor.parentItem()
            QTest.qWait(30)
            ancestor = control.parentItem()
            while ancestor:
                if ancestor.clip():
                    bounds = control.mapRectToItem(ancestor, QRectF(0, 0, control.width(), control.height()))
                    assert inside_clip(control, ancestor), (control.objectName(), "is partially clipped", bounds, ancestor.width(), ancestor.height(), ancestor.metaObject().className(), window.width(), ancestor.property("contentX"), ancestor.property("contentY"), ancestor.property("contentHeight"))
                ancestor = ancestor.parentItem()
            point = control.mapToScene(QPointF(control.width() * x_ratio, control.height() / 2)).toPoint()
            assert control.isVisible() and control.isEnabled(), "physical test target is not visible/enabled"
            assert 0 <= point.x() < target_window.width() and 0 <= point.y() < target_window.height(), "physical test target is offscreen"
            QTest.mouseClick(target_window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, point)
            QTest.qWait(25)

        info = json.loads(evaluate("JSON.stringify(Engine.bridgeInfo)", window))
        assert info["runtimeQtVersion"] == qVersion() and info["publicQtApi"] is True, info
        built = tuple(map(int, info["moduleQtVersion"].split(".")))
        runtime = tuple(map(int, info["runtimeQtVersion"].split(".")))
        assert built[0] == runtime[0] and runtime >= built, info
        expected = os.environ.get("BSI_SHADERS_EXPECT_BUILD_QT")
        if expected:
            assert info["moduleQtVersion"] == expected, info
        print("ACTUAL loaded public Qt bridge:", json.dumps(info, sort_keys=True), flush=True)
        # The host deliberately has a changing game-like background. The whole
        # page AND footer must be opaque independently of any Settings redesign.
        from shaders.qt_capture import capture_window
        def assert_opaque_page():
            expected = native_theme.property("colors").property("primary80")
            for background in ("#ff00ff", "#00ff00"):
                evaluate("color = '" + background + "'", window)
                QTest.qWait(40)
                image = capture_window(window)
                assert not image.isNull(), "shader page capture failed"
                for x, y in ((page.width() - 12, page.height() / 2), (4, page.height() - 4)):
                    point = page.mapToScene(QPointF(x, y))
                    pixel = image.pixelColor(round(point.x() * image.width() / window.width()), round(point.y() * image.height() / window.height()))
                    assert pixel == expected and pixel.alpha() == 255, ("shader page/footer leaked the host background", background, x, y, pixel.name(), pixel.alpha(), expected.name())
        if os.environ.get("BSI_SHADERS_TEST_TRANSPARENT_PAGE") == "1":
            evaluate("color = 'transparent'")  # separately expected-to-fail control
        assert_opaque_page()
        original_panel = native_theme.property("colors").property("primary80").name()
        evaluate("UiTheme.colors.primary80 = '#1f3549'", window)
        assert_opaque_page()  # the backing must follow the live native theme
        evaluate("UiTheme.colors.primary80 = '" + original_panel + "'", window)
        native_controls = {"shaderSave": "UiButton", "shaderPresets": "UiComboBox",
                           "shaderEnabled": "UiCheckBox", "shaderValue-saturation": "UiSlider",
                           "shadersScroll": "UiScrollView", "shaderFooterInfo": "UiImage"}
        from shaders.native_ui_fixture import assert_component
        for name, kind in native_controls.items():
            assert_component(item(name), kind)
        if os.environ.get("BSI_SHADERS_NATIVE_UI_DIR"):
            assert evaluate("status === 1", item("shaderFooterInfo")), "the actual native MenuInfo asset did not load"
            for control in [child for child in all_items() if child.objectName().startswith("shaderValue-")]:
                assert evaluate("handle.gradient == null && background.gradient == null", control), "shell decoration altered a native slider"
            assert evaluate("pColorState.borderGradient == null ? background.item.gradient == null : background.item.gradient === pColorState.borderGradient", item("shaderSave")), "shell decoration replaced the native Save material"
        assert evaluate("pSize === UiTheme.buttons.sizes.medium", item("shaderSave"))
        assert evaluate("pColor === UiTheme.buttons.colors.accent", item("shaderSave"))
        reference = window.findChild(QQuickItem, "stockSaveReference")
        assert reference
        assert item("shaderSave").property("implicitWidth") == reference.property("implicitWidth")
        assert item("shaderSave").property("implicitHeight") == reference.property("implicitHeight")
        assert item("shaderSave").property("font") == reference.property("font"), "Save font differs from the native button"
        initial = settings()
        assert initial["enabled"] is False and initial["potatoMode"] == 0 and len(initial) == 14
        assert evaluate("Engine.display.available"), evaluate("Engine.display.detail")
        assert evaluate("Engine.metrics.requestedRenderer") == "Automatic"
        assert evaluate("Engine.metrics.configuredRenderer") == "Vulkan"
        assert evaluate("Engine.metrics.gameSurfaceObserved") is False, "a config hint/loader is not observed game evidence"
        assert native_display.commits == 0 and not native_display.writes, "opening must not change native Display"
        assert not page.property("pEnableSave") and not window.property("bsiShaderDirty"), "a new neutral profile must not start dirty"
        save_button = item("shaderSave")
        assert save_button.isVisible() and not save_button.isEnabled()
        assert abs(save_button.property("opacity") - .4) < .001, "clean Save must match the native 40% opacity"
        assert not any(child.objectName() == "shaderDiscard" for child in all_items()), "Discard belongs to the native tab/close prompt, not the page footer"
        footer_position = save_button.mapToItem(page, QPointF(0, 0))
        assert abs(footer_position.x() + save_button.width() - page.width()) < 1
        assert item("shaderRendererStatus").property("text")
        from shaders.native_save_pixels import assert_save_pixels
        assert_save_pixels(window, page, save_button, reference, evaluate, QTest, QPointF, Qt)
        click(item("shaderEnabled"))
        assert page.property("pEnableSave") and window.property("bsiShaderDirty"), "unsaved drafts must participate in native tab/close prompts"
        assert settings() == initial, "unsaved native UI edits must not affect the live parameters"
        slider = item("shaderValue-saturation")
        assert slider.property("enabled")
        click(slider, .8)
        assert evaluate("draft.saturation") > 0, "real pointer input did not move the shader slider"
        assert settings() == initial
        click(item("shaderSave"))
        changed = settings()
        assert changed["enabled"] and changed["saturation"] > 0
        assert not page.property("pEnableSave") and not window.property("bsiShaderDirty")
        assert profile.is_file(), "the actual native bridge did not persist the expected test-mode profile"
        evaluate("setValue('contrast', 31)")
        assert page.property("pEnableSave") and settings() == changed
        # The page intentionally has no permanent Discard action. This is the
        # callback used by the native prompt; its physical No/Discard paths are
        # exercised in shaders_native_settings_test.py.
        evaluate("fDiscardChanges()")
        assert not page.property("pEnableSave") and settings() == changed
        click(item("shaderEnhance"))
        assert evaluate("presetIndex") == 4 and page.property("pEnableSave")
        assert settings() == changed, "Enhance must remain a draft until Save"
        click(item("shaderSave"))
        enhanced = settings()
        assert all(enhanced[key] > 0 for key in ("saturation", "contrast", "hdr", "sharpen", "fxaa", "bloom"))
        assert save_button.isVisible() and not save_button.isEnabled() and abs(save_button.property("opacity") - .4) < .001
        assert save_button.mapToItem(page, QPointF(0, 0)) == footer_position, "feedback must not move the native-style footer"
        keys = list(initial.keys())
        keys.remove("enabled")
        keys.remove("potatoMode")
        keys.remove("potatoGraphic")
        keys.remove("textureOverridesReviewed")
        for width in (750, 420, 750):
            window.setWidth(width)
            QTest.qWait(30)
            form = item("shaderForm")
            viewport = item("shaderViewport")
            extent = math.ceil(form.property("implicitHeight")) + 20
            assert viewport.property("contentHeight") == extent, "scroll extent must follow the settled form layout"
            QTest.qWait(50)
            assert viewport.property("contentHeight") == extent, "idle form sizing did not settle"
            minimum_width = evaluate("Layout.minimumWidth", form)
            assert minimum_width <= form.width() + .5, ("form minimum overflows the viewport", width, minimum_width, form.width())
            for child in form.childItems():
                if child.isVisible():
                    assert child.x() >= -.5 and child.x() + child.width() <= form.width() + .5, ("form row overflows horizontally", width, child.objectName(), child.x(), child.width(), form.width())
            if width == 420 and os.environ.get("BSI_SHADERS_TEST_FONTLESS") == "1":
                heading = item("shaderResolutionHeading")
                assert heading.property("implicitWidth") > form.width(), "font-isolated control did not exercise wide fallback metrics"
                assert heading.width() <= form.width() and heading.height() > evaluate("bsiTheme.fonts.bodyLarge.pixelSize"), "wide fallback heading must wrap instead of widening every row"
            for key in keys:
                reset_button = item("shaderControlReset-" + key)
                value_label = item("shaderValueLabel-" + key)
                # Native Qt 5/6 UiButton content is a Row/Column layout, not
                # our former hand-painted Text. Inspect the actual label inside.
                children, labels = list(reset_button.childItems()), []
                while children:
                    child = children.pop()
                    children.extend(child.childItems())
                    if child.metaObject().indexOfProperty("text") >= 0 and child.property("text") == reset_button.property("text"):
                        labels.append(child)
                assert labels, "native Reset label is missing: " + key
                for label in labels:
                    assert not label.property("truncated"), "Reset text was elided: " + key
                    assert label.width() + .5 >= label.property("implicitWidth"), "Reset text does not fit: " + key
                assert abs(reset_button.x() + reset_button.width() - reset_button.parentItem().width()) < 1, "Reset is not right-aligned: " + key
                assert value_label.x() + value_label.width() <= reset_button.x() - 11, "value and Reset overlap: " + key
                evaluate("setValue('" + key + "', 31)")
                assert reset_button.isEnabled()
                click(reset_button)
                assert evaluate("draft['" + key + "']") == 0
                assert not reset_button.isEnabled() and settings() == enhanced, "row Reset must not save implicitly"
                assert evaluate("activeFocus", viewport) and not evaluate("activeFocus", reset_button), "a self-disabling Reset must hand focus back to the viewport, not trigger stale row auto-scrolling"
        evaluate("fDiscardChanges()")
        # Exercise preset/reset value semantics without pretending these
        # function calls are physical category navigation in HD-Player.
        evaluate("preset(2)")
        assert evaluate("draft.saturation") == 22 and evaluate("draft.hdr") == 18
        evaluate("reset()")
        click(item("shaderSave"))
        assert settings() == initial
        evaluate("reset()")
        assert not page.property("pEnableSave"), "map insertion order must not create false unsaved changes"
        # Real popup/keyboard selection, real Save button and actual C++ native
        # adapter. Eligibility can remove a mode, so a visual row is NOT its
        # persisted mode number. Nothing is written before modal confirmation.
        def options():
            return json.loads(evaluate("JSON.stringify(resolutionOptions)"))

        def select_potato(mode, wait_poll=False):
            rows = options()
            index = next(index for index, row in enumerate(rows) if row["mode"] == mode)
            combo = item("shaderPotatoModes")
            assert combo.isEnabled()
            click(combo)
            assert evaluate("popup.visible", combo), "physical click did not open the native-style mode popup"
            if wait_poll:
                QTest.qWait(1150)
                assert evaluate("popup.visible", combo), "unchanged status polling rebuilt/closed the user's open combo"
            QTest.keyClick(window, Qt.Key.Key_Home)
            for _ in range(index):
                QTest.keyClick(window, Qt.Key.Key_Down)
            QTest.keyClick(window, Qt.Key.Key_Return)
            QTest.qWait(30)
            assert evaluate("draft.potatoMode") == mode

        def restart_dialog(staged):
            dialogs = [candidate for candidate in QGuiApplication.allWindows()
                       if candidate.objectName() == "shaderRestartPrompt" and candidate.isVisible()]
            assert len(dialogs) == 1, ("stock restart dialog missing/duplicated", warnings)
            dialog = dialogs[0]
            assert_component(dialog, "UiMessageDialog")
            # PySide's transientParent() pointer-return ownership heuristic can
            # wrongly invalidate the host wrapper when the JS-owned dialog dies.
            # Compare the very same live QWindow identities inside QML instead.
            assert evaluate("restartPrompt.transientParent === page.Window.window")
            assert dialog.modality() == Qt.WindowModality.ApplicationModal
            assert dialog.title() == "Restart " + native_display.productName
            assert dialog.property("pDescription") == "Some of the changes need " + native_display.productName + " to restart. Do you want to restart now?"
            assert dialog.property("staged") is staged and not native_display.pIsShortcutsEnabled
            assert evaluate("pButtonsList.pButtons.length", dialog) == 2
            assert evaluate("pButtonsList.pButtons[0].pColor === UiTheme.buttons.colors.accent", dialog)
            assert evaluate("pButtonsList.pButtons[1].pColor === UiTheme.buttons.colors.secondary", dialog)
            return dialog

        def dialog_button(dialog, index):
            label = evaluate("pButtonsList.pButtons[" + str(index) + "].pText", dialog)
            for control in all_items(dialog.contentItem()):
                if control.metaObject().indexOfProperty("text") >= 0 and control.property("text") == label:
                    if control.metaObject().className().startswith("UiButton_") or control.property("bsiFixtureKind") == "UiButton":
                        assert_component(control, "UiButton")
                        return control
            raise AssertionError("No physical native dialog button: " + label)

        def confirm_size():
            dialog = restart_dialog(True)
            assert dialog_button(dialog, 0).property("text") == "Restart Now"
            assert dialog_button(dialog, 1).property("text") == "Discard Changes"
            click(dialog_button(dialog, 0), target_window=dialog)
            assert native_display.pIsShortcutsEnabled and not evaluate("!!restartPrompt")

        # A genuine texture switch has its own persisted draft/acknowledgement,
        # never silently substitutes a resolution change or a final-image blur.
        click(item("shaderPotatoGraphic"))
        assert evaluate("draft.potatoGraphic") and not settings()["potatoGraphic"]
        review = item("shaderPotatoReview")
        assert inside_clip(review, item("shaderViewport")), "the required review must be revealed beside the toggle, not buried after a long paragraph"
        assert "Not saved" in item("shaderPotatoTextureStatus").property("text")
        assert "Current saved setting: Off" in item("shaderPotatoTextureStatus").property("text")
        click(save_button)
        assert not page.property("saveFailed") and "not saved yet" in page.property("feedback")
        assert evaluate("needsTextureReview") and not settings()["potatoGraphic"] and not native_display.writes
        assert inside_clip(review, item("shaderViewport")) and evaluate("activeFocus", review)
        # The C++ safety validation remains strict even if QML is bypassed.
        refused = json.loads(evaluate("JSON.stringify(Engine.apply(draft))"))
        assert not refused["ok"] and not settings()["potatoGraphic"] and not native_display.writes
        click(review)
        assert "Not saved" in item("shaderPotatoTextureStatus").property("text") and not page.property("feedback")
        click(save_button)
        assert settings()["potatoGraphic"] and settings()["textureOverridesReviewed"] and not settings()["enabled"]
        assert native_display.commits == 0 and not native_display.writes and not evaluate("!!restartPrompt")
        assert "Saved setting: On" in item("shaderPotatoTextureStatus").property("text")
        assert evaluate("Engine.metrics.textureDraws") == 0, "a saved switch without an observed game draw is not a successful sampler pass"
        assert "No texture override confirmed" in item("shaderPotatoTextureStatus").property("text")
        assert "Inspected calls: 0" in item("shaderPotatoTextureStatus").property("text")
        assert "Sampler-inspection calls: 0" in item("shaderPotatoTextureStatus").property("text")
        assert evaluate("Engine.metrics.textureDepthIndependent"), "saved On must arm the actual depth-independent runtime policy"
        assert item("shaderFooterStatus").property("text") == "Potato saved; no texture override confirmed"
        click(item("shaderCopyStatus"))
        diagnostics = QGuiApplication.clipboard().text()
        assert "calls=0; sampler-inspection calls=0; conservative depth bypass=0" in diagnostics
        assert "Texture sampling policy: Owned mip compatibility (depth independent)" in diagnostics
        assert "opaque mipmapped in-game HUD may also change" in diagnostics
        assert "test disabled=0; writes disabled=0; NEVER=0; ALWAYS=0; missing attachment=0; under 16 bits=0; unspecified=0" in diagnostics
        assert "Nested API calls: inspected=0; prepared=0" in diagnostics
        assert "not additional game frames or proof of game-resource ownership" in diagnostics
        assert "not measured game pixels/FPS" in diagnostics
        click(item("shaderEnhance")); click(item("shaderReset"))
        assert evaluate("draft.potatoGraphic") and not page.property("pEnableSave"), "visual presets/reset must preserve texture choice"
        click(item("shaderPotatoReview")); click(item("shaderPotatoGraphic")); click(save_button)
        assert settings() == initial and not native_display.writes

        original_display = dict(native_display.committed)

        def external_size(width, height, custom=False):
            native_display.committed = dict(original_display, fb_width=width, fb_height=height, custom_resolution_selected=custom)
            native_display.draft = dict(native_display.committed)
            assert evaluate("Engine.bindNativeDisplay(uiBackend)")
            QTest.qWait(40)

        # The user's at/below-profile failure: current-size choices are honest
        # no-ops, ineligible upscaling choices are absent, and effects still Save.
        for width, height, mode, expected_modes in ((1280, 720, 1, [0, 1, 2]), (960, 540, 2, [0, 2])):
            external_size(width, height)
            assert [row["mode"] for row in options()] == expected_modes, options()
            assert any(row["mode"] == mode and "current; no resize" in row["text"] for row in options())
            if height == 540:
                assert "already at the 540 px short-edge minimum" in item("shaderResolutionAvailability").property("text")
            select_potato(mode, wait_poll=True)
            click(item("shaderEnhance"))
            click(save_button)
            assert settings()["potatoMode"] == mode and settings()["enabled"]
            assert native_display.commits == 0 and not native_display.writes and native_display.restarts == 0
            assert not evaluate("!!restartPrompt") and not evaluate("Engine.display.restartRequired")
            assert "already" in page.property("feedback").lower() and "no Display write" in page.property("feedback")
            assert "no Display write or restart" in item("shaderPotatoPreview").property("text")
            select_potato(0); click(item("shaderReset")); click(save_button)
            assert settings() == initial and not native_display.writes

        # A previously valid unsaved choice can become ineligible externally.
        # Preserve all other drafts; offer a stock action to drop ONLY that size.
        external_size(1280, 720)
        select_potato(1)
        click(item("shaderEnhance"))
        external_size(960, 540)
        assert [row["mode"] for row in options()] == [0, 2]
        click(save_button)
        assert page.property("saveFailed") and not evaluate("!!restartPrompt") and not native_display.writes
        click(item("shaderResolutionKeepSaved"))
        assert evaluate("draft.enabled && draft.sharpen > 0 && draft.potatoMode === 0")
        click(save_button)
        assert settings()["enabled"] and not native_display.writes
        click(item("shaderReset")); click(save_button)
        external_size(original_display["fb_width"], original_display["fb_height"], original_display["custom_resolution_selected"])
        assert settings() == initial

        select_potato(2)
        assert page.property("pEnableSave") and window.property("bsiShaderDirty")
        assert settings() == initial and native_display.commits == 0 and not native_display.writes
        assert "960" in item("shaderPotatoPreview").property("text")
        evaluate("fDiscardChanges()")
        assert not page.property("pEnableSave") and evaluate("draft.potatoMode") == 0 and not native_display.writes
        # A separate native factory, not a golden screenshot or our component,
        # is the pixel oracle for the requested stock Graphics restart style.
        evaluate("openRestartReference()", window)
        reference_dialog = next(candidate for candidate in QGuiApplication.allWindows()
                                if candidate.objectName() == "stockRestartReference" and candidate.isVisible())
        assert_component(reference_dialog, "UiMessageDialog")
        QTest.mouseMove(reference_dialog, QPointF(10, reference_dialog.height() - 10).toPoint())
        expected_restart_pixels = capture_window(reference_dialog)
        if os.environ.get("BSI_SHADERS_TEST_FONTLESS") != "1":
            # Change one glyph, not the title's length/line count. Missing-font
            # boxes must fail the text oracle, not pass via unrelated reflow.
            reference_dialog.setTitle(reference_dialog.title().replace("Restart", "Reztart", 1))
            changed_restart_pixels = capture_window(reference_dialog)
            assert changed_restart_pixels.size() == expected_restart_pixels.size() and changed_restart_pixels != expected_restart_pixels, (
                "restart pixel oracle did not detect changed painted text",
                "expectedSize", expected_restart_pixels.size(), "changedSize", changed_restart_pixels.size(),
                "identical", changed_restart_pixels == expected_restart_pixels)
        click(dialog_button(reference_dialog, 1), target_window=reference_dialog)
        select_potato(2)
        before_prompt = profile.read_bytes()
        click(save_button)
        dialog = restart_dialog(True)
        QTest.mouseMove(dialog, QPointF(10, dialog.height() - 10).toPoint())
        actual_restart_pixels = capture_window(dialog)
        assert actual_restart_pixels == expected_restart_pixels, ("restart dialog differs from the stock UiMessageDialog factory", actual_restart_pixels.size(), expected_restart_pixels.size())
        print("PASS: restart dialog matches a separate stock UiMessageDialog pixel-for-pixel; changed painted-title negative control" if os.environ.get("BSI_SHADERS_TEST_FONTLESS") != "1" else "PASS: restart resource geometry/material matches with isolated missing-font metrics (not painted-text certification)", flush=True)
        assert settings() == initial and native_display.commits == 0 and not native_display.writes
        assert len(dialog.property("expectedReviewKey")) == 64
        refused = json.loads(evaluate("JSON.stringify(Engine.applyReviewed(draft, ''))"))
        assert not refused["ok"] and not native_display.writes, "missing review stamp must never fall back to an unreviewed native write"
        assert profile.read_bytes() == before_prompt and page.property("pEnableSave"), "opening a stock restart prompt must not save first"
        # A real modal blocks underlying Settings input; Discard means there is
        # genuinely nothing to undo on disk or in the native backend.
        point = save_button.mapToScene(QPointF(save_button.width() / 2, save_button.height() / 2)).toPoint()
        QTest.mouseClick(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, point)
        QTest.qWait(30)
        assert len([candidate for candidate in QGuiApplication.allWindows() if candidate.objectName() == "shaderRestartPrompt" and candidate.isVisible()]) == 1
        click(dialog_button(dialog, 1), target_window=dialog)
        assert settings() == initial and not page.property("pEnableSave") and not native_display.writes
        assert profile.read_bytes() == before_prompt and native_display.pIsShortcutsEnabled
        select_potato(2); click(save_button)
        dialog = restart_dialog(True)
        close = next(control for control in all_items(dialog.contentItem())
                     if control.isVisible() and control.isEnabled() and (control.objectName() == "nativeDialogClose"
                         or (control.metaObject().indexOfProperty("asset") >= 0 and control.property("asset") == "CloseIcon")))
        click(close, target_window=dialog)
        assert not evaluate("!!restartPrompt") and page.property("pEnableSave") and not native_display.writes, "closing the stock prompt must keep the unsaved draft"
        assert native_display.pIsShortcutsEnabled
        # Rejected restarts keep this authored host alive for subsequent checks.
        native_display.reject_restart = True
        click(save_button); confirm_size()
        assert settings()["potatoMode"] == 2 and settings()["enabled"] is False, "real native workload reduction must be independent of the postprocess enable flag"
        assert native_display.committed["fb_width"] == 960 and native_display.committed["fb_height"] == 540
        assert native_display.committed["custom_resolution_selected"] is True and native_display.commits == 1
        stored = json.loads(profile.read_text())
        assert stored["schema"] == "bsi.shaders.v3" and stored["display"]["original"] == {"width": 1920, "height": 1080, "custom": False}
        assert "pendingDisplay" not in stored and evaluate("Engine.display.restartRequired")
        assert "restart" in item("shaderPotatoStatus").property("text").lower()
        assert save_button.mapToItem(page, QPointF(0, 0)) == footer_position, "Potato feedback must not move Save"
        click(item("shaderEnhance"))
        assert evaluate("draft.potatoMode") == 2, "visual presets must not silently change native size"
        click(item("shaderReset"))
        assert evaluate("draft.potatoMode") == 2 and not page.property("pEnableSave"), "Reset visuals must not silently disable Potato"
        select_potato(0)
        click(save_button); confirm_size()
        assert settings() == initial and native_display.committed == original_display and native_display.commits == 2
        assert native_display.discards == 0, "shader Discard must not globally discard native settings"
        assert all(key in ("fb_width", "fb_height", "custom_resolution_selected") for key, _ in native_display.writes)
        failure = evaluate("JSON.stringify(Engine.apply({enabled:true,saturation:NaN}))")
        assert json.loads(failure)["ok"] is False
        assert settings() == initial, "invalid input changed the live state"
        evaluate("setValue('enabled', true)")
        evaluate("fDiscardChanges()")
        assert not page.property("pEnableSave") and settings() == initial
        assert evaluate("Engine.bindNativeDisplay(null)") is False
        assert not item("shaderPotatoModes").isEnabled() and not evaluate("Engine.display.available"), "missing native methods must disable Potato, not display a working-looking no-op"
        evaluate("preset(2)")
        click(save_button)
        assert settings()["saturation"] == 22 and native_display.commits == 2, "a missing Display adapter must not disable independent visual settings"
        evaluate("reset()")
        click(save_button)
        assert settings() == initial and native_display.committed == original_display
        # Preserve the independently implemented resolution recovery action too:
        # its longer native button must remain physically reachable at 420 px,
        # including with actual player controls and missing-font metrics.
        assert evaluate("Engine.bindNativeDisplay(uiBackend)")
        select_potato(2)
        click(save_button); confirm_size()
        external = dict(native_display.committed, fb_width=1600, fb_height=900, custom_resolution_selected=True)
        native_display.committed = dict(external)
        native_display.draft = dict(external)
        assert evaluate("Engine.bindNativeDisplay(uiBackend)")
        window.setWidth(420)
        QTest.qWait(100)
        assert evaluate("Engine.display.conflict")
        keep, modes = item("shaderPotatoKeep"), item("shaderPotatoModes")
        assert keep.isVisible() and keep.isEnabled() and not modes.isEnabled()
        writes, commits = list(native_display.writes), native_display.commits
        click(keep)
        assert evaluate("keepCurrentDisplay") and page.property("pEnableSave")
        click(save_button)
        assert settings() == initial and not page.property("pEnableSave")
        assert native_display.committed == external and native_display.draft == external
        assert native_display.writes == writes and native_display.commits == commits
        stored = json.loads(profile.read_text())
        assert stored["display"] == {} and "pendingDisplay" not in stored
        assert not evaluate("Engine.display.conflict") and not keep.isVisible()
        # A newer same-engine visual Save changes no eligibility/display facts,
        # but still invalidates the modal's captured stamp. Retry must capture
        # the refreshed preview instead of remaining stuck on the stale key.
        window.setWidth(750)
        select_potato(2); click(save_button)
        dialog = restart_dialog(True)
        stale_review_key = dialog.property("expectedReviewKey")
        commits, writes, restarts = native_display.commits, list(native_display.writes), native_display.restarts
        updated = json.loads(evaluate("JSON.stringify(Engine.apply(Object.assign({}, Engine.saved, {contrast:14})))"))
        assert updated["ok"] and settings()["contrast"] == 14 and settings()["potatoMode"] == 0
        newer_profile = profile.read_bytes()
        click(dialog_button(dialog, 0), target_window=dialog)
        assert page.property("saveFailed") and "changed" in page.property("feedback")
        assert profile.read_bytes() == newer_profile and settings()["contrast"] == 14
        assert native_display.commits == commits and native_display.writes == writes and native_display.restarts == restarts
        click(save_button)
        dialog = restart_dialog(True)
        assert dialog.property("expectedReviewKey") != stale_review_key, "retry retained an invalidated preview after the newer shader Save"
        click(dialog_button(dialog, 1), target_window=dialog)
        assert not page.property("pEnableSave") and evaluate("draft.contrast") == 14 and profile.read_bytes() == newer_profile
        click(item("shaderReset")); click(save_button)
        assert settings() == initial and native_display.commits == commits and native_display.writes == writes
        # Revalidate after the stock modal opens: a late unrelated native draft
        # must not be globally committed/discarded, or change the reviewed size.
        window.setWidth(750)
        select_potato(2); click(save_button)
        dialog = restart_dialog(True)
        commits, writes, restarts = native_display.commits, list(native_display.writes), native_display.restarts
        native_display.draft["unrelated"] = 99
        assert evaluate("Engine.bindNativeDisplay(uiBackend)")
        click(dialog_button(dialog, 0), target_window=dialog)
        assert page.property("saveFailed") and "changed" in page.property("feedback")
        assert native_display.commits == commits and native_display.writes == writes and native_display.restarts == restarts
        assert native_display.draft["unrelated"] == 99 and native_display.discards == 0
        assert settings() == initial and page.property("pEnableSave")
        native_display.draft = dict(native_display.committed)
        assert evaluate("Engine.bindNativeDisplay(uiBackend)")
        click(save_button); confirm_size()
        assert evaluate("Engine.display.restartRequired && Engine.display.canRestart")
        assert page.property("saveFailed") and "saved resolution" in page.property("feedback") and window.isVisible()
        commits, writes, restarts = native_display.commits, list(native_display.writes), native_display.restarts
        restart = item("shaderResolutionRestart")
        click(restart)
        dialog = restart_dialog(False)
        assert dialog_button(dialog, 1).property("text") == "Later", "already-saved changes must not offer a fake Discard"
        click(dialog_button(dialog, 1), target_window=dialog)
        assert not evaluate("!!restartPrompt") and native_display.restarts == restarts and window.isVisible()
        assert native_display.commits == commits and native_display.writes == writes
        click(restart)
        dialog = restart_dialog(False)
        click(dialog_button(dialog, 0), target_window=dialog)
        assert page.property("saveFailed") and native_display.restarts == restarts + 1 and window.isVisible()
        assert evaluate("Engine.display.restartRequired"), "a refused restart must retain the saved/manual-restart requirement"
        native_display.reject_restart = False
        click(restart)
        dialog = restart_dialog(False)
        click(dialog_button(dialog, 0), target_window=dialog)
        assert native_display.restarts == restarts + 2 and not window.isVisible()
        assert window.property("restartLoaderCount") == 1, "only an accepted native restart should show the Settings restart loader"
        assert native_display.commits == commits and native_display.writes == writes, "restart must never commit again or write other config"
        assert not evaluate("Engine.display.restartRequired"), "accepted request must not be issued twice from a stale button"
        # Missing public restartPlayer must not lose the available resolution
        # setting or label Save as an automatic restart. Same stock shell, honest
        # manual notice, and no fake Discard after the configuration is saved.
        window.show()
        assert not evaluate("Engine.bindNativePlayer(null)")
        assert not evaluate("Engine.display.restartAvailable")
        select_potato(0); click(save_button)
        dialog = restart_dialog(True)
        assert not dialog.property("automaticRestart") and "manually" in dialog.property("pNotice")
        assert dialog_button(dialog, 0).property("text") == "Save changes"
        click(dialog_button(dialog, 0), target_window=dialog)
        assert settings() == initial and window.isVisible() and not page.property("saveFailed")
        assert native_display.commits == commits + 1 and native_display.restarts == restarts + 2
        assert evaluate("Engine.display.restartRequired && !Engine.display.canRestart")
        assert "Restart the player" in page.property("feedback")
        commits, writes = native_display.commits, list(native_display.writes)
        select_potato(2); click(save_button)
        dialog = restart_dialog(True)
        click(dialog_button(dialog, 1), target_window=dialog)
        assert not page.property("pEnableSave") and settings() == initial
        assert native_display.commits == commits and native_display.writes == writes and native_display.discards == 0
        assert not warnings, "QML runtime warnings:\n" + "\n".join(warnings)
        window.setVisible(False)
        window.deleteLater()
        QTest.qWait(20)
        engine.deleteLater()
        app.processEvents()
    print("PASS: real C++ plugin; visible physical Potato review/unsaved-vs-runtime feedback and strict backend guard; 720/540 current-size no-op saves, no upscaling, eligible-mode mapping/popup survives status polling, stale resolution-only draft reset preserves effects; stock modal restart resource/model/theme/transient identity, pre-commit Discard/close/modal/stale-native-draft/newer-save gates with refreshed retry preview, native restart refusal/retry/Later/acceptance/manual fallback, no extra commits/global discard and accepted-only restart loader; exact native Save pixels/fixed footer, physical effects and ten Reset rows at two widths, isolated persistence and clean QML bindings")
    print("LIMIT: fixture window only; native player installation and category navigation, Windows hooks, DirectX/Vulkan and hardware GPU coverage are not certified")


if __name__ == "__main__":
    main()
