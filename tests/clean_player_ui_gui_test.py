"""Physically exercise scenes emitted by the shipping EXE/resource patchers.

Set BSI_TOPBAR_TEST_FIXTURES when running --selftest first. These are authored
contract scenes, not player code. No Python rewrite or emulator is executed.
"""
import os
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QUICK_BACKEND", "software")
QT5 = os.environ.get("BSI_TEST_QT_API") == "PyQt5"
os.environ.setdefault("QT_QUICK_CONTROLS_STYLE", "Default" if QT5 else "Basic")
if QT5:
    from PyQt5.QtCore import QCoreApplication, QLibraryInfo, QMetaObject, QPointF, Qt, qVersion
    from PyQt5.QtGui import QFontDatabase, QFontInfo, QGuiApplication
    from PyQt5.QtQml import QQmlComponent, QQmlEngine
    from PyQt5.QtQuick import QQuickItem, QQuickWindow
    from PyQt5.QtTest import QSignalSpy, QTest
else:
    os.environ.setdefault("QSG_RHI_BACKEND", "software")
    from PySide6.QtCore import QCoreApplication, QLibraryInfo, QMetaObject, QPointF, Qt, qVersion
    from PySide6.QtGui import QFontDatabase, QFontInfo, QGuiApplication
    from PySide6.QtQml import QQmlComponent, QQmlEngine
    from PySide6.QtQuick import QQuickItem, QQuickWindow
    from PySide6.QtTest import QSignalSpy, QTest


def tree(window):
    pending = list(window.contentItem().childItems())
    result = {}
    while pending:
        item = pending.pop()
        if item.objectName() in {"topbarMenu", "playerTitleBar", "playerName", "detailsRow", "versionLabel", "instanceLabel",
                                 "backButton", "minimizeButton", "closeButton", "nativeSettingsButton", "settingsHeading",
                                 "settingsVersionText", "settingsArea", "menuGlyph"}:
            assert item.objectName() not in result, item.objectName()
            result[item.objectName()] = item
        pending.extend(item.childItems())
    return result


def click(window, item):
    point = item.mapToScene(QPointF(item.width() / 2, item.height() / 2)).toPoint()
    QTest.mouseClick(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, point)
    QTest.qWait(10)


def main():
    root = Path(os.environ["BSI_TOPBAR_TEST_FIXTURES"])
    expected = [f"{model}{callback}-{phase}.qml"
                for model in ("element", "listmodel")
                for callback in ("", "-callback")
                for phase in ("stock", "clean", "clean-details", "details-clean", "restored")]
    assert all((root / name).is_file() for name in expected), "Run the production --selftest fixture exporter first"
    # install-qt-action leaves the C++ build SDK's QML imports in the environment.
    # In Windows CI that made this Qt 5 host parse Qt 6's qmldir ("version auto"),
    # before any authored scene could load. Native/QML plugins must be the
    # independent Python host's own; no source/assertion or player Qt is changed.
    if QT5:
        plugins = QLibraryInfo.location(QLibraryInfo.PluginsPath)
        qml = QLibraryInfo.location(QLibraryInfo.Qml2ImportsPath)
    else:
        plugins = QLibraryInfo.path(QLibraryInfo.LibraryPath.PluginsPath)
        qml = QLibraryInfo.path(QLibraryInfo.LibraryPath.QmlImportsPath)
    assert Path(plugins).is_dir() and Path(qml).is_dir(), (plugins, qml)
    os.environ["QT_PLUGIN_PATH"] = plugins
    os.environ["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(Path(plugins) / "platforms")
    os.environ.pop("QML2_IMPORT_PATH", None)
    os.environ.pop("QML_IMPORT_PATH", None)
    QCoreApplication.setLibraryPaths([plugins])
    app = QGuiApplication.instance() or QGuiApplication([])
    # Qt 5's Windows offscreen QPA uses QFreeTypeFontDatabase, not Windows'
    # installed-font discovery. Without an explicitly deployed font it uses
    # the box engine: "Settings" measures 144x18 but paints zero glyph pixels.
    # Make this independent authored host self-contained, like the other Qt
    # fixtures. Do not change any production-written QML or paint threshold.
    previous_family = QFontInfo(app.font()).family()
    font_path = Path(__file__).resolve().parents[1] / "src/qml/fonts/Inter.ttf"
    font_id = QFontDatabase.addApplicationFont(str(font_path))
    assert font_id >= 0, ("Could not load the bundled offscreen test font", str(font_path))
    families = QFontDatabase.applicationFontFamilies(font_id)
    assert families, ("The bundled offscreen test font has no family", str(font_path))
    font = app.font()
    font.setFamily(families[0])
    app.setFont(font)
    resolved_family = QFontInfo(app.font()).family()
    assert resolved_family in families, ("Offscreen host did not resolve its deployed font", families, resolved_family)
    print("Topbar fixture font:", str(font_path), "family:", resolved_family,
          "previous default:", repr(previous_family), flush=True)
    engine = QQmlEngine()
    engine.setImportPathList([qml, "qrc:/qt-project.org/imports", "qrc:/qt/qml"])
    print("Topbar fixture host Qt:", qVersion(), "QML paths:", engine.importPathList(), flush=True)
    warnings = []
    engine.warnings.connect(lambda errors: warnings.extend(e.toString() for e in errors))
    painted_counts = []
    for filename in expected:
        phase = next(p for p in ("clean-details", "details-clean", "restored", "stock", "clean")
                     if filename.endswith("-" + p + ".qml"))
        cleaned = phase in ("clean", "clean-details", "details-clean")
        details_hidden = phase in ("clean-details", "details-clean", "restored")
        component = QQmlComponent(engine, str((root / filename).resolve()))
        window = component.create()
        assert window is not None, "\n".join(e.toString() for e in component.errors())
        QTest.qWait(60)
        items = tree(window)
        menu, bar, name = (items[k] for k in ("topbarMenu", "playerTitleBar", "playerName"))
        for height in (32, 48, 64):
            assert window.setProperty("barHeight", height)
            for width in (720, 1068, 1440):
                window.resize(width, 300)
                assert QMetaObject.invokeMethod(window, "fReloadTopbarElements") is not False
                QTest.qWait(20)
                assert menu.isVisible() != cleaned, (filename, "native model reload re-showed the menu")
                before = window.property("menuVisibilityCallbacks")
                QMetaObject.invokeMethod(window, "forceNativeMenu")
                QMetaObject.invokeMethod(window, "forceNativeDetails")
                QTest.qWait(10)
                assert menu.isVisible() != cleaned, (filename, "imperative backend/pin-mode assignment defeated cleanup")
                if "-callback-" in filename and cleaned:
                    assert window.property("menuVisibilityCallbacks") > before, "native visibility callback was replaced"
                assert items["detailsRow"].isVisible() != details_hidden, filename
                assert items["versionLabel"].isVisible() != details_hidden, filename
                assert items["instanceLabel"].isVisible() != details_hidden, filename
                assert name.isVisible() and name.property("text") == "MSI App Player"
                assert items["versionLabel"].property("text") == "5.15.4 test"
                assert items["instanceLabel"].property("text") == "Nougat 32"
                if details_hidden:
                    centre = name.mapToItem(bar, QPointF(name.width() / 2, name.height() / 2)).y()
                    top = name.mapToItem(bar, QPointF(0, 0)).y()
                    bottom = bar.height() - top - name.height()
                    assert abs(centre - bar.height() / 2) <= .6, (filename, height, width, centre)
                    assert abs(top - bottom) <= 1.2, (filename, top, bottom)
                    assert name.mapToItem(bar, QPointF(0, 0)).x() < width / 3, "name was horizontally centred instead"
                for key in ("backButton", "minimizeButton", "closeButton", "nativeSettingsButton", "settingsHeading", "settingsVersionText"):
                    assert items[key].isVisible() and items[key].isEnabled(), (filename, key)
                assert items["settingsHeading"].property("text") == "Settings"
                assert items["settingsVersionText"].property("text") == "Version and instance name"
        # Hiding/re-showing the parent window must not resurrect a child menu.
        window.hide()
        QTest.qWait(10)
        window.show()
        QMetaObject.invokeMethod(window, "fReloadTopbarElements")
        QTest.qWait(30)
        assert menu.isVisible() != cleaned, filename
        before = window.property("menuClicks")
        click(window, menu)
        assert window.property("menuClicks") == before + (0 if cleaned else 1), (filename, "menu hit target was not removed/restored")
        before = window.property("settingsClicks")
        click(window, items["nativeSettingsButton"])
        assert window.property("settingsClicks") == before + 1, (filename, "sidebar Settings no longer accepts physical clicks")
        # The matching source checks above are not enough: Settings text must
        # actually paint, and the hidden menu's subtree must paint nothing.
        def rendered():
            # Request a scene-update boundary after property changes before
            # scheduling the asynchronous item grab, and keep independent
            # pixel snapshots. This is capture synchronization hardening, not
            # a diagnosed fix for the Windows Qt 5 missing-paint failure.
            window.requestUpdate()
            QTest.qWait(40)
            capture = window.contentItem().grabToImage()
            spy = QSignalSpy(capture.ready)
            if capture.image().isNull():
                assert spy.wait(2000), filename
            result = capture.image()
            assert not result.isNull(), filename
            return result.copy()

        image = rendered()
        label = items["settingsHeading"]
        origin = label.mapToScene(QPointF(0, 0))
        # A resolved font/property is not proof that the label paints. Map
        # logical bounds to capture pixels and keep the differential check.
        # Prove this very Text paints: render it at zero opacity with unchanged
        # Column geometry, then demand foreground contrast against that control
        # image. A hidden/blank/nonpainting label still fails, not a relaxed
        # source-only or 'some bright pixel anywhere' assertion.
        opacity = label.property("opacity")
        assert opacity > 0
        label.setProperty("opacity", 0)
        control = rendered()
        label.setProperty("opacity", opacity)
        assert (image.width(), image.height()) == (control.width(), control.height())
        sx, sy = image.width() / window.width(), image.height() / window.height()
        ink = 0
        for y in range(round(origin.y() * sy), min(image.height(), round((origin.y() + label.height()) * sy))):
            for x in range(round(origin.x() * sx), min(image.width(), round((origin.x() + label.width()) * sx))):
                painted, blank = image.pixelColor(x, y), control.pixelColor(x, y)
                if (painted.red() + painted.green() + painted.blue()
                        > blank.red() + blank.green() + blank.blue() + 96):
                    ink += 1
        if ink < 5:
            # Preserve the actual pair for a failed platform; do not suppress or
            # weaken the pixel assertion when a Windows font/render path fails.
            api = "qt5" if QT5 else "qt6"
            assert image.save(str(root / (filename + "-painted-" + api + ".png")))
            assert control.save(str(root / (filename + "-control-" + api + ".png")))
            print("Settings paint failure:", filename, "Qt", qVersion(), "font", label.property("font").toString(),
                  "bounds", origin.x(), origin.y(), label.width(), label.height(),
                  "capture", image.width(), image.height(), "pixel delta", ink, flush=True)
        assert ink >= 5, (filename, "Settings label did not paint", ink, sx, sy)
        painted_counts.append(ink)
        if filename == "element-clean-details.qml":
            assert image.save(str(root / ("clean-topbar-qt5.png" if QT5 else "clean-topbar-qt6.png")))
        window.hide()
        window.deleteLater()
        QTest.qWait(10)
    assert not warnings, "\n".join(warnings)
    print("Settings label differential pixels: minimum", min(painted_counts), "across", len(painted_counts), "scenes", flush=True)
    print("PASS: 20 production-written topbar scenes, both native models/owning-item shapes and callbacks; menu stays hidden after reload/imperative show/resize/window restore; physical Settings input and painted text survive; one combined removal collapses details and gives equal top/bottom name spacing in Qt " + ("5" if QT5 else "6"))
    print("LIMIT: authored native-contract scenes, not an installed player's native window or physical GPU verification")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
