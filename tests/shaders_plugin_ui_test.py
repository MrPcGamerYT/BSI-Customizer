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
    from PyQt5.QtCore import QCoreApplication, QFileInfo, QLibraryInfo, QObject, QPointF, QRectF, QStandardPaths, Qt, QUrl, qVersion
    from PyQt5.QtGui import QGuiApplication
    from PyQt5.QtQml import QQmlComponent, QQmlEngine, QQmlExpression
    from PyQt5.QtQuick import QQuickItem
    from PyQt5.QtTest import QTest
else:
    from PySide6.QtCore import QCoreApplication, QFileInfo, QLibraryInfo, QObject, QPointF, QRectF, QStandardPaths, Qt, QUrl, qVersion
    from PySide6.QtGui import QGuiApplication
    from PySide6.QtQml import QQmlComponent, QQmlEngine, QQmlExpression
    from PySide6.QtQuick import QQuickItem
    from PySide6.QtTest import QTest


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
        # shader success/failure. No controls or native types are mocked.
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
Window {
    id: host
    width: 750; height: 470; visible: true
    property alias shaderPage: shaderLoader.item
    property bool bsiShaderDirty: false
    Bootstrap { hostWindow: host; instanceName: "Fixture-Nougat64"; graphicsRenderer: "gl" }
    Item {
        anchors.fill: parent
        property var bIcons: ({})
        Loader {
            id: shaderLoader
            anchors.fill: parent; anchors.margins: 12
            source: "qrc:/bsi-shaders/BsiShadersSetting.qml"
            onLoaded: { item.bsiTabIndex = 12; item.fOnQmlLoaded() }
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
        QTest.qWait(80)
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

        def click(control):
            # Reset buttons exist on every row, including below the viewport.
            # Scroll the real Flickable rather than clicking a clipped item.
            # Focus-follow scrolling is deferred after a previously focused
            # Reset becomes disabled. Let it finish, then reveal the new target;
            # never treat a click behind the clipping edge as successful input.
            for _ in range(4):
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
                clipped = False
                while ancestor:
                    if ancestor.clip():
                        clipped |= not inside_clip(control, ancestor)
                    ancestor = ancestor.parentItem()
                if not clipped:
                    break
            ancestor = control.parentItem()
            while ancestor:
                if ancestor.clip():
                    assert inside_clip(control, ancestor), (control.objectName(), "is partially clipped", ancestor.width(), ancestor.height(), ancestor.metaObject().className(), window.width(), ancestor.property("contentY"), ancestor.property("contentHeight"))
                ancestor = ancestor.parentItem()
            point = control.mapToScene(QPointF(control.width() / 2, control.height() / 2)).toPoint()
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
        initial = settings()
        assert initial["enabled"] is False and len(initial) == 11
        assert not page.property("pEnableSave") and not window.property("bsiShaderDirty"), "a new neutral profile must not start dirty"
        save_button = item("shaderSave")
        assert save_button.isVisible() and not save_button.isEnabled()
        assert abs(save_button.property("opacity") - .4) < .001, "clean Save must match the native 40% opacity"
        assert not any(child.objectName() == "shaderDiscard" for child in all_items()), "Discard belongs to the native tab/close prompt, not the page footer"
        footer_position = save_button.mapToItem(page, QPointF(0, 0))
        assert abs(footer_position.x() + save_button.width() - page.width()) < 1
        assert item("shaderRendererStatus").property("text")
        click(item("shaderEnabled"))
        assert page.property("pEnableSave") and window.property("bsiShaderDirty"), "unsaved drafts must participate in native tab/close prompts"
        assert settings() == initial, "unsaved native UI edits must not affect the live parameters"
        slider = item("shaderValue-saturation")
        assert slider.property("enabled")
        point = slider.mapToScene(QPointF(slider.width() * .8, slider.height() / 2)).toPoint()
        QTest.mouseClick(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, point)
        QTest.qWait(20)
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
        for width in (750, 420, 750):
            window.setWidth(width)
            QTest.qWait(30)
            form = item("shaderForm")
            viewport = item("shaderViewport")
            extent = math.ceil(form.property("implicitHeight")) + 20
            assert viewport.property("contentHeight") == extent, "scroll extent must follow the settled form layout"
            QTest.qWait(50)
            assert viewport.property("contentHeight") == extent, "idle form sizing did not settle"
            for key in keys:
                reset_button = item("shaderControlReset-" + key)
                value_label = item("shaderValueLabel-" + key)
                assert not evaluate("contentItem.truncated", reset_button), "Reset text was elided: " + key
                assert evaluate("contentItem.width >= contentItem.implicitWidth", reset_button), "Reset text does not fit: " + key
                assert abs(reset_button.x() + reset_button.width() - reset_button.parentItem().width()) < 1, "Reset is not right-aligned: " + key
                assert value_label.x() + value_label.width() <= reset_button.x() - 11, "value and Reset overlap: " + key
                evaluate("setValue('" + key + "', 31)")
                assert reset_button.isEnabled()
                click(reset_button)
                assert evaluate("draft['" + key + "']") == 0
                assert not reset_button.isEnabled() and settings() == enhanced, "row Reset must not save implicitly"
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
        failure = evaluate("JSON.stringify(Engine.apply({enabled:true,saturation:NaN}))")
        assert json.loads(failure)["ok"] is False
        assert settings() == initial, "invalid input changed the live state"
        evaluate("setValue('enabled', true)")
        evaluate("fDiscardChanges()")
        assert not page.property("pEnableSave") and settings() == initial
        assert not warnings, "QML runtime warnings:\n" + "\n".join(warnings)
        window.setVisible(False)
        window.deleteLater()
        QTest.qWait(20)
        engine.deleteLater()
        app.processEvents()
    print("PASS: real C++ regular-plugin import, single native-style Save footer/clean opacity, physical enable/slider/Save/Enhance and all ten fully visible right-aligned Reset buttons at two widths, native discard callback, preset/reset, atomic isolated persistence and clean QML bindings")
    print("LIMIT: fixture window only; native player installation and category navigation, Windows hooks, DirectX/Vulkan and hardware GPU coverage are not certified")


if __name__ == "__main__":
    main()
