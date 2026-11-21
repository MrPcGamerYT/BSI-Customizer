"""Real-pixel capture contract, independent of a shader module or OEM assets.

Run separately with PyQt5/PySide6. This rejects blank/stale images and checks
physical DPI scaling, immutable snapshots and a nonpainting pixel negative.
"""
import os

from shaders_plugin_ui_test import (
    configure_host_qt, QGuiApplication, QQmlEngine, QQmlComponent, QUrl,
    QTest, qVersion,
)
from shaders.qt_capture import capture_window


def main():
    configure_host_qt()
    app = QGuiApplication.instance() or QGuiApplication([])
    from shaders.qt_fonts import install_readable_test_font
    install_readable_test_font(app)
    app.setQuitOnLastWindowClosed(False)
    engine = QQmlEngine()
    component = QQmlComponent(engine)
    component.setData(b'''import QtQuick 2.15
import QtQuick.Window 2.15
Window {
 width:160; height:100; visible:false
 property color patchColor: "#56c4a1"
 property real patchOpacity: 1
 property string labelText: "Read s"
 property bool showText: true
 Rectangle { anchors.fill:parent; color:"#152435" }
 Rectangle { x:32; y:24; width:64; height:40; color:patchColor; opacity:patchOpacity }
 Text { x:8; y:72; visible:showText; text:labelText; color:"white"; font.pixelSize:14 }
}
''', QUrl("CaptureContract.qml"))
    assert component.status() == QQmlComponent.Status.Ready, [e.toString() for e in component.errors()]
    window = component.create()
    assert window is not None, [e.toString() for e in component.errors()]
    try:
        # Qt 5 offscreen QPA can reinterpret its first cached geometry at
        # fractional DPI. Set this tiny fixture's logical extent once AFTER
        # realization; do not change/compensate production Settings geometry.
        window.create()
        app.processEvents()
        window.show()
        window.resize(160, 100)
        QTest.qWait(20)
        assert (window.width(), window.height()) == (160, 100)
        # A null grabWindow on Qt 5 software is the diagnosed unsupported API,
        # not a skip/pass of any shader pixel assertion. The public item path
        # must still return the real, fully painted scene below.
        QTest.qWait(40)
        direct_null = window.grabWindow().isNull()
        first = capture_window(window)

        def color(image, x, y):
            return image.pixelColor(round(x * image.width() / window.width()),
                                    round(y * image.height() / window.height())).name()

        def painted(image, expected):
            assert color(image, 64, 44) == expected, "the real patch did not paint"
            assert color(image, 8, 8) == "#152435", "the real foundation did not paint"

        painted(first, "#56c4a1")
        window.setProperty("patchColor", "#da9857")
        second = capture_window(window)
        painted(second, "#da9857")
        painted(first, "#56c4a1")  # a later render must not mutate this snapshot
        assert first.size() == second.size()
        window.setProperty("patchOpacity", 0.0)
        blank = capture_window(window)
        try:
            painted(blank, "#da9857")
        except AssertionError as error:
            assert str(error) == "the real patch did not paint"
        else:
            raise AssertionError("a nonpainting patch passed the unchanged pixel assertion")
        assert color(blank, 64, 44) == "#152435"
        # Equal-length text with no surrounding layout change must alter real
        # glyph pixels, not merely a QObject title or a row's geometry. This
        # independently catches a box-engine/missing-font host before the plugin.
        def glyph_delta(before, after):
            assert after.size() == before.size() and after != before, "readable glyph pixels did not change"

        window.setProperty("labelText", "Read z")
        changed_text = capture_window(window)
        glyph_delta(blank, changed_text)
        assert color(changed_text, 64, 44) == "#152435"
        assert color(changed_text, 8, 8) == "#152435"
        window.setProperty("showText", False)
        hidden_text = capture_window(window)
        window.setProperty("labelText", "Read s")
        changed_hidden_text = capture_window(window)
        try:
            glyph_delta(hidden_text, changed_hidden_text)
        except AssertionError as error:
            assert str(error) == "readable glyph pixels did not change"
        else:
            raise AssertionError("a nonpainting text change passed the unchanged glyph assertion")
        print("PASS: Qt", qVersion(), "actual asynchronous pixels, readable glyph delta/nonpainting text negative, fresh bindings, immutable snapshots and strict nonpainting patch negative;",
              "directWindowCaptureNull=" + str(direct_null), "scale=" + os.environ.get("QT_SCALE_FACTOR", "host"),
              "image=" + str((first.width(), first.height())), flush=True)
    finally:
        window.hide()
        window.deleteLater()
        QTest.qWait(20)


if __name__ == "__main__":
    main()
