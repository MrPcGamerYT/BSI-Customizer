"""Read-only Qt evidence inventory tests. No test executes any PE or DLL."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("emulator_qt_inventory", ROOT / "scripts/Inspect-EmulatorQt.py")
inventory = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(inventory)


def fixture(*, dll=False, major=5, release="5.15.4.0", tag=None, cache=None, qt_names=None):
    """Synthetic, never executable PE with independent fixed-version resources."""
    data = bytearray(0xA000)
    def put(offset, value, width=4):
        struct.pack_into({2: "<H", 4: "<I", 8: "<Q"}[width], data, offset, value)
    def raw(offset, value):
        data[offset:offset + len(value)] = value
    raw(0, b"MZ"); put(0x3C, 0x80); raw(0x80, b"PE\0\0")
    put(0x84, 0x8664, 2); put(0x86, 3, 2); put(0x94, 240, 2)
    put(0x96, 0x2022 if dll else 0x22, 2); put(0x98, 0x20B, 2)
    put(0xA8, 0x1000); put(0xB0, 0x400000, 8)
    put(0xB8, 0x1000); put(0xBC, 0x200); put(0xD0, 0xA000); put(0xD4, 0x1000); put(0x104, 16)
    for index, (name, start, length, flags) in enumerate(((b".text", 0x1000, 0x1000, 0x60000020),
                                                        (b".rdata", 0x2000, 0x6000, 0x40000040),
                                                        (b".rsrc", 0x8000, 0x2000, 0x40000040))):
        at = 0x188 + index * 40
        raw(at, name); put(at + 8, length); put(at + 12, start); put(at + 16, length); put(at + 20, start); put(at + 36, flags)
    data[0x1000] = 0xC3
    if not dll:
        put(0x110, 0x2000); put(0x114, 100)
        pool = 0x3000
        def append(value):
            nonlocal pool
            at = pool; raw(at, value + b"\0"); pool += len(value) + 1
            return at
        for i, part in enumerate(("Core", "Gui", "Qml", "Quick")):
            name = append((qt_names or {}).get(part, f"Qt{major}{part}.dll").encode())
            pool = (pool + 7) & ~7
            symbols = [b"?fixture@@YAXXZ"] + ([f"qt_version_tag_{tag.replace('.', '_')}".encode()] if tag and i == 0 else [])
            table = pool; iat = table + (len(symbols) + 1) * 8; pool = iat + (len(symbols) + 1) * 8
            for j, symbol in enumerate(symbols):
                symbol_at = append(b"\0\0" + symbol)
                put(table + 8 * j, symbol_at, 8); put(iat + 8 * j, symbol_at, 8)
            at = 0x2000 + i * 20
            put(at, table); put(at + 12, name); put(at + 16, iat)
    put(0x118, 0x8000); put(0x11C, 0x2000)
    put(0x800E, 1, 2); put(0x8010, 16); put(0x8014, 0x80000020)
    put(0x802E, 1, 2); put(0x8030, 1); put(0x8034, 0x80000040)
    put(0x804E, 1, 2); put(0x8050, 1033); put(0x8054, 0x60)
    put(0x8060, 0x8100); put(0x8064, 92)
    put(0x8100, 92, 2); put(0x8102, 52, 2)
    raw(0x8106, "VS_VERSION_INFO\0".encode("utf-16le")); put(0x8128, 0xFEEF04BD); put(0x812C, 0x10000)
    a, b, c, d = map(int, release.split("."))
    for offset, value in ((0x8130, a << 16 | b), (0x8134, c << 16 | d), (0x8138, a << 16 | b),
                          (0x813C, c << 16 | d), (0x8140, 0x3F), (0x8148, 0x40004), (0x814C, 2 if dll else 1)):
        put(offset, value)
    if cache:
        a, b, c = map(int, cache.split("."))
        raw(0x7600, b"qv4cdata"); put(0x7608, 0x29); put(0x760C, a << 16 | b << 8 | c)
        put(0x7618, 512); put(0x7670, 0); put(0x7674, 248)
    return bytes(data)


def snapshot(folder):
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.iterdir() if p.is_file()}


class InventoryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="bsi-qt-inventory-")
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.exe = self.folder / "HD-Player.exe"
        self.exe.write_bytes(fixture(cache="5.15.4"))

    def runtimes(self, major=5, release="5.15.4.0"):
        for part in inventory.REQUIRED:
            (self.folder / f"Qt{major}{part.capitalize()}.dll").write_bytes(fixture(dll=True, major=major, release=release))

    def test_compiler_is_not_runtime(self):
        before = snapshot(self.folder)
        report = inventory.inspect(self.exe)
        self.assertEqual(report["qmlCacheCompilerHeaders"], {"5.15.4": 1})
        self.assertIsNone(report["runtimeQtVersion"])
        self.assertFalse(report["runtimeInventoryComplete"])
        self.assertFalse(report["playerExecuted"] or report["targetModified"])
        self.assertEqual(snapshot(self.folder), before)

    def test_runtime_fixed_version_not_compiler_or_display_version(self):
        self.runtimes(release="5.15.19.0")
        before = snapshot(self.folder)
        report = inventory.inspect(self.exe)
        self.assertEqual(report["runtimeQtVersion"], "5.15.19")
        self.assertTrue(report["runtimeInventoryComplete"])
        self.assertEqual(report["qmlCacheCompilerHeaders"], {"5.15.4": 1})
        self.assertEqual(report["shaderSupport"], "not-certified-by-this-inventory")
        self.assertEqual(snapshot(self.folder), before)

    def test_minor_import_tag_does_not_invent_patch(self):
        self.exe.write_bytes(fixture(major=6, release="5.22.75.6322", tag="6.7"))
        report = inventory.inspect(self.exe)
        self.assertEqual(report["player"]["compiledMinorTags"], ["6.7"])
        self.assertEqual(report["qtMajor"], 6)
        self.assertIsNone(report["runtimeQtVersion"])
        self.runtimes(6, "6.7.3.0")
        self.assertEqual(inventory.inspect(self.exe)["runtimeQtVersion"], "6.7.3")

    def test_mixed_debug_and_suffixed_import_families_refuse(self):
        self.runtimes()
        for name in ("Qt6Gui.dll", "Qt5Guid.dll", "Qt5Gui_custom.dll"):
            with self.subTest(name=name):
                self.exe.write_bytes(fixture(qt_names={"Gui": name}))
                report = inventory.inspect(self.exe)
                self.assertIsNone(report["runtimeQtVersion"])
                self.assertFalse(report["runtimeInventoryComplete"])
                self.assertTrue(any("import family" in text for text in report["issues"]))

    def test_mixed_runtime_versions_refuse_complete_result(self):
        self.runtimes()
        (self.folder / "Qt5Gui.dll").write_bytes(fixture(dll=True, release="5.15.19.0"))
        report = inventory.inspect(self.exe)
        self.assertIsNone(report["runtimeQtVersion"])
        self.assertTrue(any("mixed fixed" in text for text in report["issues"]))

    def test_debug_wrong_arch_and_product_mismatch(self):
        for offset, value, width in ((0x8144, 1, "<I"), (0x84, 0x14C, "<H"), (0x813C, 0, "<I")):
            with self.subTest(offset=offset):
                self.runtimes()
                data = bytearray((self.folder / "Qt5Gui.dll").read_bytes())
                struct.pack_into(width, data, offset, value)
                (self.folder / "Qt5Gui.dll").write_bytes(data)
                report = inventory.inspect(self.exe)
                self.assertIsNone(report["runtimeQtVersion"])
                self.assertFalse(report["runtimeInventoryComplete"])

    @unittest.skipIf(sys.platform == "win32", "Windows filesystem normally folds case")
    def test_case_ambiguous_runtime(self):
        self.runtimes()
        (self.folder / "qt5core.dll").write_bytes(fixture(dll=True))
        with self.assertRaisesRegex(inventory.InventoryError, "Case-ambiguous"):
            inventory.inspect(self.exe)

    @unittest.skipIf(sys.platform == "win32", "No symlink privilege assumption")
    def test_linked_runtime_and_parent_refuse(self):
        self.runtimes()
        (self.folder / "Qt5Gui.dll").unlink()
        (self.folder / "Qt5Gui.dll").symlink_to(self.folder / "Qt5Core.dll")
        with self.assertRaisesRegex(inventory.InventoryError, "Linked/reparse"):
            inventory.inspect(self.exe)
        linked = self.folder / "linked-directory"
        linked.symlink_to(self.folder, target_is_directory=True)
        with self.assertRaisesRegex(inventory.InventoryError, "Linked/reparse"):
            inventory.inspect(linked / self.exe.name)

    def test_zip_is_read_without_extracting_or_borrowing_adjacent_runtime(self):
        self.runtimes()
        path = self.folder / "reference.zip"
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("../../HD-Player.exe", fixture(cache="5.15.4"))
        before = snapshot(self.folder)
        report = inventory.inspect(path)
        self.assertIsNone(report["runtimeQtVersion"])
        self.assertTrue(report["archiveSha256"])
        self.assertEqual(snapshot(self.folder), before)
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr("a/HD-Player.exe", fixture())
            archive.writestr("b/HD-PLAYER.EXE", fixture())
        with self.assertRaisesRegex(inventory.InventoryError, "exactly one"):
            inventory.inspect(path)

    def test_invalid_file_and_invalid_cache_header(self):
        self.exe.write_bytes(b"not a PE")
        with self.assertRaises(inventory.InventoryError):
            inventory.inspect(self.exe)
        self.exe.write_bytes(fixture(dll=True))
        with self.assertRaisesRegex(inventory.InventoryError, "EXE"):
            inventory.inspect(self.exe)
        data = bytearray(fixture(cache="5.15.4"))
        struct.pack_into("<I", data, 0x7618, 0xFFFFFFFF)
        self.exe.write_bytes(data)
        self.assertEqual(inventory.inspect(self.exe)["qmlCacheCompilerHeaders"], {})

    @unittest.skipUnless(os.environ.get("BSI_QT_REFERENCE_DIR"), "Optional hash-pinned EXE-only evidence, never executed")
    def test_four_pinned_references(self):
        folder = Path(os.environ["BSI_QT_REFERENCE_DIR"])
        manifest = json.loads((ROOT / "tests/fixtures/emulator-qt-evidence.json").read_text())
        for expected in manifest["samples"]:
            with self.subTest(sample=expected["input"]):
                path = folder / expected["input"]
                before = hashlib.sha256(path.read_bytes()).hexdigest()
                report = inventory.inspect(path)
                self.assertEqual(report["player"]["sha256"], expected["playerSha256"])
                self.assertEqual(report["player"]["fileVersion"], expected["playerFileVersion"])
                self.assertEqual(report["qtMajor"], expected["qtMajor"])
                self.assertEqual(report["player"]["compiledMinorTags"], expected["compiledMinorTags"])
                self.assertEqual(report["qmlCacheCompilerHeaders"], expected["qmlCacheCompilerHeaders"])
                self.assertIsNone(report["runtimeQtVersion"])
                self.assertFalse(report["playerExecuted"] or report["targetModified"])
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), before)
                print("PINNED QT EVIDENCE:", json.dumps({key: report[key] for key in ("input", "qtMajor", "qmlCacheCompilerHeaders", "runtimeQtVersion")}), flush=True)


if __name__ == "__main__":
    unittest.main()
