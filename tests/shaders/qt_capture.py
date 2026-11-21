"""Public, fresh scene capture on independent Qt 5/6 software hosts.

QQuickWindow.grabWindow() is unsupported by Qt 5's software scene graph and
returns a null image. Capture the actual content tree asynchronously instead;
never substitute a screenshot/golden, property value, retry or weaker pixel
assertion. A single update boundary lets pending bindings reach the scene.
"""
import os


def capture_window(window):
    if os.environ.get("BSI_TEST_QT_API") == "PyQt5":
        from PyQt5.QtTest import QSignalSpy, QTest
    else:
        from PySide6.QtTest import QSignalSpy, QTest
    assert window.isVisible() and window.width() > 0 and window.height() > 0, "cannot capture an unrealized test window"
    window.requestUpdate()
    QTest.qWait(40)
    capture = window.contentItem().grabToImage()
    assert capture is not None, "the Qt content capture could not be scheduled"
    ready = QSignalSpy(capture.ready)
    if capture.image().isNull():
        assert ready.wait(2000), "Qt did not finish the content capture"
    image = capture.image()
    assert not image.isNull(), "Qt returned an empty content capture"
    # Keep snapshots independent after a following property change or capture.
    return image.copy()
