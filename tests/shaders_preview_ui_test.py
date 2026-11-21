"""Live preview stays usable after stock-control delegation (authored host only).

This is not the OEM-control pixel suite or an installed player. The full live
harness must share one complete theme across the shell and real plugin/page,
register explicit root-QRC contracts, and retain the requested opaque layout.
"""
import importlib.util
import os
from pathlib import Path

from shaders_plugin_ui_test import configure_host_qt


def main():
    configure_host_qt()
    root = Path(__file__).resolve().parents[1]
    spec = importlib.util.spec_from_file_location("shader_preview", root / ".harness/settings_window_preview.py")
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    preview = m.SettingsPreview(shader_module=os.environ["BSI_SHADERS_MODULE_ROOT"])
    try:
        m.QTest.qWait(200)
        for msi in (True, False):
            preview.event({"type": "palette", "msi": msi})
            expected = m.QColor("#21242c" if msi else "#101a27")
            for index in range(13):
                state = preview.event({"type": "navigate", "index": index})
                m.QTest.qWait(20)
                assert state["selected"] == index and not state["discardPrompt"]
                assert (state["visibleWidth"], state["visibleHeight"]) == (750, 470)
                tree = list(m.items(preview.window.contentItem()))
                frame = next(i for i in tree if m.prop(i, "radius") == 14)
                assert frame.property("color") == expected and not m.evaluate(frame, "gradient != null")
                image = m.QImage.fromData(preview.frame())
                point = frame.mapToScene(m.QPointF(12, 30))
                assert image.pixelColor(int(point.x() * image.width() / preview.window.width()),
                                        int(point.y() * image.height() / preview.window.height())) == expected
            save = next(i for i in tree if i.objectName() == "shaderSave")
            page = next(i for i in tree if i.metaObject().indexOfProperty("bsiTabIndex") >= 0)
            assert page.property("color") == expected
            assert save.property("bsiFixtureKind") == "UiButton", "the preview must not resurrect removed custom controls"
            assert m.evaluate(page, "bsiTheme === UiTheme"), "shell and Shaders must share the same full live theme"
            assert not save.isEnabled() and save.property("opacity") == .4
            checkbox = next(i for i in tree if i.objectName() == "shaderEnabled")
            combo = next(i for i in tree if i.objectName() == "shaderPresets")
            assert m.evaluate(checkbox, "palette.windowText === UiTheme.colors.primary10"), "sample checkbox text must stay readable"
            assert m.evaluate(combo, "palette.buttonText === UiTheme.colors.primary10 && palette.button === UiTheme.colors.primary70"), "sample selector must follow the live host palette"
        def click(item):
            assert item.isVisible() and item.isEnabled()
            point = item.mapToScene(m.QPointF(item.width() / 2, item.height() / 2))
            for kind in ("down", "up"):
                preview.event({"type": kind, "x": point.x(), "y": point.y()})
            m.QTest.qWait(30)

        enabled = next(i for i in tree if i.objectName() == "shaderEnabled")
        click(enabled)
        assert preview.state()["shaderDirty"] and save.isEnabled()
        preview.event({"type": "navigate", "index": 11})
        m.QTest.qWait(30)
        assert preview.state()["discardPrompt"] and preview.state()["selected"] == 12
        prompt_items = list(m.items(preview.window.contentItem()))
        keep = next(i for i in prompt_items if i.objectName() == "previewKeepChanges")
        discard = next(i for i in prompt_items if i.objectName() == "previewDiscardChanges")
        for button in (keep, discard):
            assert button.property("bsiFixtureKind") == "UiButton"
        click(keep)
        assert not preview.state()["discardPrompt"] and preview.state()["shaderDirty"]
        assert preview.state()["selected"] == 12, "No must preserve the draft and current tab"
        preview.event({"type": "navigate", "index": 11})
        m.QTest.qWait(30)
        assert preview.state()["discardPrompt"]
        click(discard)
        assert preview.state()["selected"] == 11
        assert not preview.state()["discardPrompt"] and not preview.state()["shaderDirty"]
        assert not preview.warnings, "\n".join(preview.warnings)
        print("PASS: live real-plugin preview, explicit root-QRC contracts/full shared theme, all 13 physical tab selections in two palettes, exact opaque frame pixels, native-contract Save, physical No/Discard and unchanged 750x470 geometry; authored host, not OEM/player/GPU certification")
    finally:
        preview.close()


if __name__ == "__main__":
    main()
