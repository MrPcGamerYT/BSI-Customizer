"""Production-written topbar captures without a system-font dependency.

Run the complete strict GUI regression on the host and with isolated font
search paths. A separately staged transparent-label negative must fail the
original painted-pixel assertion. Never rewrite a positive fixture or retry a
failed render. Requires BSI_TOPBAR_TEST_FIXTURES from the native --selftest.
"""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
from xml.sax.saxutils import escape


def main():
    root = Path(os.environ["BSI_TOPBAR_TEST_FIXTURES"]).resolve()
    sources = {p.name: p.read_bytes() for p in root.glob("*.qml")}
    assert sources, "Export the production-written topbar scenes with --selftest first"
    digests = {name: hashlib.sha256(data).digest() for name, data in sources.items()}
    api = "qt5" if os.environ.get("BSI_TEST_QT_API") == "PyQt5" else "qt6"
    output = root / "capture-validation" / api
    empty_fonts = output / "empty-fonts"
    empty_fonts.mkdir(parents=True, exist_ok=True)
    assert not list(empty_fonts.iterdir()), "The isolated font directory must stay empty"
    fontconfig = output / "fonts.conf"
    fontconfig.write_text('<?xml version="1.0"?>\n<fontconfig><dir>'
                          + escape(empty_fonts.as_posix()) + '</dir><cachedir>'
                          + escape((output / "fontconfig-cache").as_posix())
                          + '</cachedir></fontconfig>\n', encoding="utf-8")
    test = Path(__file__).with_name("clean_player_ui_gui_test.py")
    for case in ("host-default", "isolated-fonts", "transparent-label-negative"):
        directory = output / case
        directory.mkdir(parents=True, exist_ok=True)
        for name, data in sources.items():
            (directory / name).write_bytes(data)
        negative = case == "transparent-label-negative"
        if negative:
            # Deliberately break only the first label's paint, retaining its
            # text, positive opacity, dimensions and every native callback.
            # Original production-written inputs above remain byte-identical.
            path = directory / "element-stock.qml"
            scene = path.read_text(encoding="utf-8")
            heading = 'objectName: "settingsHeading"; text: "Settings"; color: "#e7edf5"'
            assert scene.count(heading) == 1, "Locate exactly the authored negative-control label"
            path.write_text(scene.replace(heading, heading.replace('"#e7edf5"', '"transparent"')),
                            encoding="utf-8")
        env = dict(os.environ, BSI_TOPBAR_TEST_FIXTURES=str(directory))
        if case != "host-default":
            # On Windows Qt 5 offscreen, FreeType reads QT_QPA_FONTDIR rather
            # than installed Windows fonts. On Linux, additionally isolate
            # Fontconfig so developer-machine fonts cannot mask this defect.
            env.update(QT_QPA_FONTDIR=str(empty_fonts), FONTCONFIG_FILE=str(fontconfig),
                       FONTCONFIG_PATH=str(empty_fonts))
        try:
            result = subprocess.run([sys.executable, "-X", "faulthandler", "-u", str(test)],
                                    env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, encoding="utf-8", errors="replace", timeout=90)
        except subprocess.TimeoutExpired as error:
            partial = error.stdout or b""
            if isinstance(partial, bytes):
                partial = partial.decode("utf-8", errors="replace")
            (directory / "result.log").write_text(partial, encoding="utf-8")
            print(partial, end="", flush=True)
            raise AssertionError(f"{case}: GUI regression timed out; no pass or retry") from error
        (directory / "result.log").write_text(result.stdout, encoding="utf-8")
        if negative:
            valid = result.returncode == 1 and "AssertionError: ('element-stock.qml', 'Settings label did not paint', 0," in result.stdout
        else:
            valid = result.returncode == 0 and "PASS: 20 production-written topbar scenes" in result.stdout
        assert all(hashlib.sha256((root / name).read_bytes()).digest() == digest
                   for name, digest in digests.items()), "A production-written input fixture was changed"
        if not valid:
            print(result.stdout, end="", flush=True)
            raise AssertionError(f"{case}: unexpected GUI-test result {result.returncode}")
        if negative:
            print("PASS: isolated transparent label rejected by the unchanged >=5 painted-pixel / >96 RGB-sum contrast assertion", flush=True)
        else:
            print(f"PASS: {case}\n{result.stdout}", end="", flush=True)
    print("PASS: host and font-isolated production-written scenes plus a strict nonpainting negative; original QML inputs unchanged")


if __name__ == "__main__":
    main()
