"""Filesystem-only host path selection tests; fake files are never loaded."""
from pathlib import Path
import tempfile
import unittest

from qt_host_paths import windows_host_binaries


class HostBinaryPathTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="bsi-qt-host-paths-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "binding"
        self.root.mkdir()
        # Windows TEMP may use RUNNER~1; production deliberately returns the
        # canonical long path. Compare identities in that same representation.
        self.root = self.root.resolve()

    def dll(self, directory, major=6):
        directory.mkdir(parents=True, exist_ok=True)
        (directory / f"Qt{major}Core.dll").write_bytes(b"fixture only; never loaded")
        return directory.resolve()

    def test_flat_pyside_wheel_with_nonexistent_reported_bin(self):
        self.dll(self.root)
        self.assertEqual(windows_host_binaries(self.root, self.root / "bin", 6), self.root)

    def test_nested_pyqt_wheel(self):
        binaries = self.dll(self.root / "Qt5" / "bin", 5)
        self.assertEqual(windows_host_binaries(self.root, binaries, 5), binaries)

    def test_real_reported_subdirectory_preferred(self):
        self.dll(self.root)
        binaries = self.dll(self.root / "bin")
        self.assertEqual(windows_host_binaries(self.root, binaries, 6), binaries)

    def test_external_build_sdk_is_never_used(self):
        sdk = self.dll(Path(self.temp.name) / "foreign-sdk" / "bin")
        with self.assertRaisesRegex(RuntimeError, "No Qt 6 host Core DLL"):
            windows_host_binaries(self.root, sdk, 6)
        self.dll(self.root)
        self.assertEqual(windows_host_binaries(self.root, sdk, 6), self.root)

    def test_parent_segments_return_canonical_host_identity(self):
        self.dll(self.root)
        (self.root / "nested").mkdir()
        alias = self.root / "nested" / ".."
        self.assertEqual(windows_host_binaries(alias, alias / "bin", 6), self.root)

    def test_wrong_major_or_missing_core_fails_closed(self):
        self.dll(self.root, 5)
        with self.assertRaisesRegex(RuntimeError, "No Qt 6 host Core DLL"):
            windows_host_binaries(self.root, self.root, 6)
        with self.assertRaises(ValueError):
            windows_host_binaries(self.root, self.root, 7)


if __name__ == "__main__":
    unittest.main()
