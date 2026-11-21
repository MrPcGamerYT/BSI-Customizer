"""Explicit readable font for normal offscreen pixel tests, not player theming.

Windows offscreen QPA need not discover installed desktop fonts. A box-engine
fallback can reflow a title or paint identical boxes for different letters.
Load the repo's own redistributable font in the TEST process; never change the
native theme's font properties or turn missing glyphs into a skipped pass.
The separate empty-font stress process deliberately must not call this helper.
"""
import os
from pathlib import Path


def install_readable_test_font(app):
    assert os.environ.get("BSI_SHADERS_TEST_FONTLESS") != "1", "missing-font stress must not install a readable font"
    if os.environ.get("BSI_TEST_QT_API") == "PyQt5":
        from PyQt5.QtGui import QFont, QFontDatabase
    else:
        from PySide6.QtGui import QFont, QFontDatabase
    font_path = Path(__file__).resolve().parents[2] / "src/qml/fonts/Inter.ttf"
    existing_families = len(QFontDatabase().families()) if os.environ.get("BSI_TEST_QT_API") == "PyQt5" else len(QFontDatabase.families())
    font_id = QFontDatabase.addApplicationFont(str(font_path))
    assert font_id >= 0, "normal pixel tests require the bundled readable font: " + str(font_path)
    families = QFontDatabase.applicationFontFamilies(font_id)
    assert families, "the readable test font registered no family"
    app.setFont(QFont(families[0], 10))
    print("Readable test font:", families[0], "registered; pre-existing families=" + str(existing_families)
          + "; native theme font properties unchanged", flush=True)
    return families[0]
