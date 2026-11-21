"""Compare the page's Save against a separate native control at the same pixels.

No reference screenshot/golden is authored or bundled. Both use the registered
UiButton implementation and one live theme. A separately run background-opacity
negative must fail this exact pixel equality, not a looser fallback assertion.
"""
import os
from .qt_capture import capture_window


def assert_save_pixels(window, page, save, reference, evaluate, QTest, QPointF, Qt):
    point = save.mapToScene(QPointF(0, 0))
    for key, value in (("x", point.x()), ("y", point.y()), ("width", save.width()), ("height", save.height())):
        reference.setProperty(key, value)
    reference.setProperty("z", 1000)
    original_hover = save.property("hoverEnabled")
    save.setProperty("hoverEnabled", True)
    reference.setProperty("hoverEnabled", True)
    for state in ("clean", "enabled", "hover", "pressed"):
        evaluate("fDiscardChanges()" if state == "clean" else "setValue('contrast', 31)")
        enabled, down = state != "clean", state == "pressed"
        opacity = 1.0 if enabled else .4
        assert save.isEnabled() == enabled and abs(save.opacity() - opacity) < .001
        reference.setProperty("enabled", enabled)
        reference.setProperty("opacity", opacity)
        reference.setProperty("down", down)
        save.setProperty("down", down)
        cursor = save.mapToScene(QPointF(save.width() / 2, save.height() / 2)) if state in ("hover", "pressed") else QPointF(0, 0)
        QTest.mouseMove(window, cursor.toPoint())
        QTest.qWait(40)
        if state in ("hover", "pressed"):
            assert save.property("hovered"), "the native Save hover state was not exercised"
        if os.environ.get("BSI_SHADERS_TEST_SAVE_PIXEL_MISMATCH") == "1":
            evaluate("background.opacity = .25", save)
        actual = capture_window(window)
        assert not actual.isNull(), "native Save capture is empty"
        sx, sy = actual.width() / window.width(), actual.height() / window.height()
        rect = (round(point.x() * sx), round(point.y() * sy), round(save.width() * sx), round(save.height() * sy))
        # Opacity does not remove the actual button from its Layout (visible
        # would); it leaves precisely the same opaque page behind the reference.
        save.setProperty("opacity", 0.0)
        reference.setProperty("visible", True)
        QTest.mouseMove(window, cursor.toPoint())
        QTest.qWait(40)
        expected = capture_window(window)
        assert not expected.isNull() and actual.size() == expected.size()
        assert actual.copy(*rect) == expected.copy(*rect), ("Save differs from native UiButton pixels", state)
        reference.setProperty("visible", False)
        evaluate("opacity = Qt.binding(function() { return page.pEnableSave ? 1 : .4 })", save)
        QTest.qWait(10)
    evaluate("down = Qt.binding(function() { return pressed })", save)
    save.setProperty("hoverEnabled", original_hover)
    evaluate("fDiscardChanges()")
    QTest.mouseMove(window, QPointF(0, 0).toPoint())
    QTest.qWait(20)
    assert not page.property("pEnableSave")
    print("PASS: Save matches a separate native UiButton pixel-for-pixel in clean/enabled/hover/pressed states", flush=True)
