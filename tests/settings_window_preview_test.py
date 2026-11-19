"""Exercise the live generated-Settings preview, not the Customizer editor.

Real Qt inputs/rendered frames with explicit sample services; no EXE or VM.
Run in its own process: python tests/run_qt_runtime_test.py tests/settings_window_preview_test.py
"""
import importlib.util
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("settings_preview", REPO / ".harness/settings_window_preview.py")
preview_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preview_module)

from PySide6.QtCore import QPointF
from PySide6.QtTest import QTest


def main():
    preview = preview_module.SettingsPreview()
    state = preview.state()
    assert (state["visibleWidth"], state["visibleHeight"]) == (750, 470)
    assert state["visibleWidth"] / state["visibleHeight"] >= 1.5, "the gutter fix must keep Settings wide"
    assert preview.frame().startswith(b"\x89PNG\r\n\x1a\n")
    frames = set()
    for index, label in enumerate(preview_module.LABELS):
        state = preview.event({"type": "navigate", "index": index})
        QTest.qWait(15)
        assert state["selected"] == index and state["label"] == label
        frames.add(preview.frame())
    assert len(frames) == 12, "category navigation must paint twelve different real Qt pages"
    preview.event({"type": "navigate", "index": 0})
    QTest.qWait(20)
    tree = list(preview_module.items(preview.window.contentItem()))
    slider = next(i for i in tree if preview_module.prop(i, "to") == 540)
    # Reach lower controls by real wheel input when needed, without
    # overriding the gutter-trimmed window or clicking outside the scroll area.
    scroll = slider.parentItem()
    while preview_module.prop(scroll, "contentY") is None:
        scroll = scroll.parentItem()
    wheel_point = scroll.mapToScene(QPointF(scroll.width() / 2, scroll.height() / 2))
    for _ in range(8):
        point = slider.mapToItem(scroll, QPointF(slider.width() * .7, slider.height() / 2))
        if 10 < point.y() < scroll.height() - 10:
            break
        preview.event({"type": "wheel", "x": wheel_point.x(), "y": wheel_point.y(), "dy": 120})
        QTest.qWait(80)
    point = slider.mapToItem(scroll, QPointF(slider.width() * .7, slider.height() / 2))
    assert 0 < point.y() < scroll.height(), "the compact Settings form cannot scroll to its lower controls"
    point = slider.mapToScene(QPointF(slider.width() * .7, slider.height() / 2))
    for kind in ("down", "up"):
        preview.event({"type": kind, "x": point.x(), "y": point.y()})
    assert slider.property("value") > 250, "the live preview did not deliver real slider input"
    save_label = next(i for i in tree if preview_module.prop(i, "text") == "Save changes" and i.isVisible())
    save = save_label.parentItem()
    assert save.isEnabled(), "sample edits must enable the actual Qt Save button"
    point = save.mapToScene(QPointF(save.width() / 2, save.height() / 2))
    for kind in ("down", "up"):
        preview.event({"type": kind, "x": point.x(), "y": point.y()})
    assert preview.state()["saves"] == 1, "sample Save changes was not invoked"
    assert not save.isEnabled(), "sample Save must reset its dirty state"
    preview.event({"type": "accent", "warm": True})
    warm = preview.frame()
    preview.event({"type": "accent", "warm": False})
    assert preview.frame() != warm, "theme simulation must recolour the actual scene"
    state = preview.event({"type": "resize", "small": True})
    assert abs(state["width"] - 760 * .96) <= 1 and abs(state["height"] - 480 * .94) <= 1
    assert preview.frame().startswith(b"\x89PNG")
    state = preview.event({"type": "resize", "small": False})
    assert (state["width"], state["height"]) == (780, 500)
    close = next(i for i in tree if preview_module.prop(i, "asset") == "CloseIcon")
    point = close.mapToScene(QPointF(close.width() / 2, close.height() / 2))
    for kind in ("down", "up"):
        preview.event({"type": kind, "x": point.x(), "y": point.y()})
    assert not preview.state()["open"], "the native Settings close callback did not hide the window"
    assert preview.event({"type": "reopen"})["open"], "the preview cannot reopen after closing"
    for invalid in ({"type": "navigate", "index": 30}, {"type": "unknown"}, {"type": "down", "x": -1}):
        try:
            preview.event(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid live input was accepted: " + json.dumps(invalid))
    assert not preview.warnings, "QML runtime warnings:\n" + "\n".join(preview.warnings)
    preview.window.hide()
    preview.window.deleteLater()
    QTest.qWait(10)
    print("PASS: live generated Settings preview renders all twelve pages, accepts real tab/icon/slider/save/close input, resizes and follows theme changes; mock services only")
    return 0


if __name__ == "__main__":
    sys.exit(main())
