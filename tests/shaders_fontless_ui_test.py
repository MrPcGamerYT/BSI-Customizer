"""Run the unchanged physical plugin controls with no discoverable system fonts.

Windows offscreen QPA can use box-engine metrics instead of installed Windows
fonts. Isolate Fontconfig too so Linux reproduces the same wide-metric layout
stress. This is not a text-paint/readability certification. The independent
painted-pixel suites and their strict negative controls remain required.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from xml.sax.saxutils import escape


def main():
    with tempfile.TemporaryDirectory(prefix="bsi-shaders-no-fonts-") as temporary:
        root = Path(temporary)
        fonts = root / "empty-fonts"
        fonts.mkdir()
        fontconfig = root / "fonts.conf"
        fontconfig.write_text('<?xml version="1.0"?>\n<fontconfig><dir>'
                              + escape(fonts.as_posix()) + '</dir><cachedir>'
                              + escape((root / "fontconfig-cache").as_posix())
                              + '</cachedir></fontconfig>\n', encoding="utf-8")
        env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QPA_FONTDIR=str(fonts),
                   FONTCONFIG_FILE=str(fontconfig), FONTCONFIG_PATH=str(fonts),
                   BSI_SHADERS_TEST_FONTLESS="1")
        test = Path(__file__).with_name("shaders_plugin_ui_test.py")
        try:
            result = subprocess.run([sys.executable, "-X", "faulthandler", "-u", str(test)],
                                    env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, encoding="utf-8", errors="replace", timeout=90)
        except subprocess.TimeoutExpired as error:
            partial = error.stdout or b""
            if isinstance(partial, bytes):
                partial = partial.decode("utf-8", errors="replace")
            print(partial, end="", flush=True)
            raise AssertionError("font-isolated physical plugin test timed out; no pass or retry") from error
        print(result.stdout, end="", flush=True)
        assert result.returncode == 0, f"font-isolated physical plugin controls failed: {result.returncode}"
        print("PASS: wide missing-font metrics wrap within the form; all real Reset/Save/Potato hits retained at 750/420 px, with no repeated reveal or click retry", flush=True)


if __name__ == "__main__":
    main()
