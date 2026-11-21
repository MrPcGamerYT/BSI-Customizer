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
    from PyQt5.QtQuick import QQuickItem
    from PyQt5.QtTest import QTest
else:
    from PySide6.QtCore import Property, Signal, Slot
    from PySide6.QtCore import QCoreApplication, QFileInfo, QLibraryInfo, QObject, QPointF, QRectF, QStandardPaths, Qt, QUrl, qVersion
    from PySide6.QtGui import QGuiApplication
    from PySide6.QtQml import QQmlComponent, QQmlEngine, QQmlExpression
    from PySide6.QtQuick import QQuickItem
    from PySide6.QtTest import QTest


class NativeDisplayFixture(QObject):
    """Authored native configuration contract, NOT a real guest or restart.

    The real C++ module must invoke these typed public Qt methods. No QML mock
    replaces Engine, preferences, native-method adapter or file transaction.
    """
    flickableMovingChanged = Signal()
    pIsEditingTextfieldChanged = Signal()

    def __init__(self):
        super().__init__()
        self._moving, self._editing = False, False
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
import BSIShaders 1.0
import "qrc:/" as Native
Window {
    id: host
    width: 750; height: 470; visible: true
    color: "#ff00ff"
    property alias shaderPage: shaderLoader.item
    property int pCurrentTabIndex: 0
    property bool bsiShaderDirty: false
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

        def all_items():
            # Repeater delegates follow the visual, not QObject ownership tree.
            pending = [page]
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

        def click(control, x_ratio=.5):
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
            assert 0 <= point.x() < window.width() and 0 <= point.y() < window.height(), "physical test target is offscreen"
            QTest.mouseClick(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, point)
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
        # adapter. The config endpoint is an authored double, not a guest/FPS
        # claim. Mode drafts/Discard must never touch it before Save.
        def select_potato(index):
            combo = item("shaderPotatoModes")
            assert combo.isEnabled()
            click(combo)
            assert evaluate("popup.visible", combo), "physical click did not open the native-style mode popup"
            QTest.keyClick(window, Qt.Key.Key_Home)
            for _ in range(index):
                QTest.keyClick(window, Qt.Key.Key_Down)
            QTest.keyClick(window, Qt.Key.Key_Return)
            QTest.qWait(30)
            assert evaluate("draft.potatoMode") == index

        # A genuine texture switch has its own persisted draft/acknowledgement,
        # never silently substitutes a resolution change or a final-image blur.
        click(item("shaderPotatoGraphic"))
        assert evaluate("draft.potatoGraphic") and not settings()["potatoGraphic"]
        click(save_button)
        assert page.property("saveFailed") and not settings()["potatoGraphic"] and not native_display.writes
        click(item("shaderPotatoReview"))
        click(save_button)
        assert settings()["potatoGraphic"] and settings()["textureOverridesReviewed"] and not settings()["enabled"]
        assert native_display.commits == 0 and not native_display.writes
        assert evaluate("Engine.metrics.textureDraws") == 0, "a saved switch without an observed game draw is not a successful sampler pass"
        click(item("shaderEnhance")); click(item("shaderReset"))
        assert evaluate("draft.potatoGraphic") and not page.property("pEnableSave"), "visual presets/reset must preserve texture choice"
        click(item("shaderPotatoReview")); click(item("shaderPotatoGraphic")); click(save_button)
        assert settings() == initial and not native_display.writes

        original_display = dict(native_display.committed)
        select_potato(2)
        assert page.property("pEnableSave") and window.property("bsiShaderDirty")
        assert settings() == initial and native_display.commits == 0 and not native_display.writes
        assert "960" in item("shaderPotatoPreview").property("text")
        evaluate("fDiscardChanges()")
        assert not page.property("pEnableSave") and evaluate("draft.potatoMode") == 0 and not native_display.writes
        select_potato(2)
        click(save_button)
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
        click(save_button)
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
        click(save_button)
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
        # The save/restart distinction is visible and the actual typed native
        # restart method is only invoked AFTER two explicit clicks. A failed
        # request neither hides the player nor loses the successful save.
        window.setWidth(750)
        select_potato(2); click(save_button)
        assert evaluate("Engine.display.restartRequired && Engine.display.canRestart")
        assert not page.property("saveFailed") and "Save succeeded" in page.property("feedback")
        commits, writes = native_display.commits, list(native_display.writes)
        restart = item("shaderResolutionRestart")
        click(restart)
        assert page.property("restartConfirmation") and native_display.restarts == 0
        click(item("shaderResolutionLater"))
        assert not page.property("restartConfirmation") and native_display.restarts == 0 and window.isVisible()
        native_display.reject_restart = True
        click(restart); click(restart)
        assert page.property("saveFailed") and native_display.restarts == 1 and window.isVisible()
        assert evaluate("Engine.display.restartRequired"), "a refused restart must retain the saved/manual-restart requirement"
        native_display.reject_restart = False
        click(restart); click(restart)
        assert native_display.restarts == 2 and not window.isVisible()
        assert native_display.commits == commits and native_display.writes == writes, "restart must never commit again or write other config"
        assert not evaluate("Engine.display.restartRequired"), "accepted request must not be issued twice from a stale button"
        assert not warnings, "QML runtime warnings:\n" + "\n".join(warnings)
        window.setVisible(False)
        window.deleteLater()
        QTest.qWait(20)
        engine.deleteLater()
        app.processEvents()
    print("PASS: real C++ regular-plugin auto-hint diagnostics, independent physical texture toggle/review/Save/reset with no fake game success, native resolution save and two-click restart/Later/refusal/acceptance without extra commits; regular-plugin import, single native-style Save footer/clean opacity, physical enable/slider/Save/Enhance and all ten fully visible right-aligned Reset buttons at two widths, native discard callback, preset/reset, atomic isolated persistence, physical native-size Potato Save/Discard/restore independent of visual effects, and clean QML bindings")
    print("LIMIT: fixture window only; native player installation and category navigation, Windows hooks, DirectX/Vulkan and hardware GPU coverage are not certified")


if __name__ == "__main__":
    main()
