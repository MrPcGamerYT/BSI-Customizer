"""Real native controls/theme from pinned read-only player resources.

Never executes/loads player EXEs or proprietary DLLs, nor changes OEM QML or
ships extracted sources/artwork. Native C++ tooltip attachments and dialog
window/button-model services are authored doubles; Display remains a typed
contract endpoint. Painting/theming QML is unmodified native code. UI pixels
and input are real Qt, not game/installed-player proof.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile


def run(command, env, expected_failure=None):
    try:
        result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, encoding="utf-8", errors="replace", timeout=90)
    except subprocess.TimeoutExpired as error:
        partial = error.stdout or b""
        print(partial.decode("utf-8", errors="replace") if isinstance(partial, bytes) else partial, flush=True)
        raise AssertionError("native resource test timed out; no retry, skip or weaker input check") from error
    if expected_failure:
        assert result.returncode == 1 and expected_failure in result.stdout, result.stdout
        print("PASS: strict native-control negative rejected:", expected_failure, flush=True)
    else:
        print(result.stdout, end="", flush=True)
        assert result.returncode == 0, (command, result.returncode)


def main():
    root = Path(__file__).resolve().parents[1]
    exporter = Path(os.environ["BSI_SHADERS_IMAGE_TEST"]).resolve()
    references = Path(os.environ["BSI_QT_REFERENCE_DIR"]).resolve()
    manifest = json.loads((root / "tests/fixtures/emulator-qt-evidence.json").read_text(encoding="utf-8"))
    assert exporter.is_file() and references.is_dir() and len(manifest["samples"]) == 4
    major = 5 if os.environ.get("BSI_TEST_QT_API") == "PyQt5" else 6
    samples = [s for s in manifest["samples"] if s["qtMajor"] == major]
    assert len(samples) == 2
    base_env = dict(os.environ)
    for name in ("BSI_SHADERS_TEST_TRANSPARENT_PAGE", "BSI_SHADERS_TEST_SAVE_PIXEL_MISMATCH", "BSI_SHADERS_TEST_FONTLESS"):
        base_env.pop(name, None)
    for index, expected in enumerate(samples):
        path = references / expected["input"]
        assert path.is_file() and not path.is_symlink() and 0 < path.stat().st_size <= 256 * 1024 * 1024
        original = path.read_bytes()
        if path.suffix.lower() == ".zip":
            with zipfile.ZipFile(path) as archive:
                members = [e for e in archive.infolist() if not e.is_dir()
                           and Path(e.filename.replace("\\", "/")).name.lower() == "hd-player.exe"]
                assert len(members) == 1 and 0 < members[0].file_size <= 256 * 1024 * 1024
                payload = archive.read(members[0])
        else:
            payload = original
        assert hashlib.sha256(payload).hexdigest() == expected["playerSha256"], ("pinned reference changed", expected["input"])
        try:
            with tempfile.TemporaryDirectory(prefix="bsi-stock-controls-") as temporary:
                directory = Path(temporary)
                copy = directory / "read-only-reference.exe"
                copy.write_bytes(payload)
                extracted = directory / "native-resources"
                run([str(exporter), "--export-native-ui", str(copy), str(extracted)], base_env)
                evidence = json.loads((extracted / "inventory.json").read_text(encoding="utf-8"))
                assert evidence["referenceSha256"] == expected["playerSha256"]
                env = dict(base_env, BSI_SHADERS_NATIVE_UI_DIR=str(extracted))
                print("ACTUAL native control family:", expected["input"], "host Qt major", major, flush=True)
                for script in ("shaders_plugin_ui_test.py", "shaders_fontless_ui_test.py"):
                    run([sys.executable, "-X", "faulthandler", "-u", str(root / "tests" / script)], env)
                if index == 0:
                    script = str(root / "tests/shaders_plugin_ui_test.py")
                    run([sys.executable, "-X", "faulthandler", "-u", script],
                        dict(env, BSI_SHADERS_TEST_TRANSPARENT_PAGE="1"), "shader page/footer leaked the host background")
                    run([sys.executable, "-X", "faulthandler", "-u", script],
                        dict(env, BSI_SHADERS_TEST_SAVE_PIXEL_MISMATCH="1"), "Save differs from native UiButton pixels")
                assert copy.read_bytes() == payload, "temporary reference changed"
        finally:
            assert path.read_bytes() == original, "original reference was modified"
    print("PASS: two hash-pinned native resource families, real components/live theme, exact Save pixels in four states and stock restart-dialog pixels/changed-title negative, physical Potato review and eligible/current-size choices, pre-commit modal Discard/close/restart/refusal/Later/manual semantics, all ten physical Reset hits at 750/420 px and empty-font metrics, opaque page/footer plus strict negative controls", flush=True)
    print("LIMIT: read-only resource/Qt fixture with authored nonpainting tooltip/dialog services, not a player process, real restart, game texture LOD, Vulkan detection or GPU/driver certification", flush=True)


if __name__ == "__main__":
    main()
