"""Dependency-free checks for the DLL-startup audit (never executes a patcher).

The full audit requires the separately supplied, reviewed source files and
pefile. These checks run without them and do not install any loader feature.
"""

import hashlib
import importlib.util
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

REPO = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = REPO / "scripts/Test-DllStartup-Patcher.py"
spec = importlib.util.spec_from_file_location("dll_startup_audit", SCRIPT)
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class AuditGuardTest(unittest.TestCase):
    def invoke(self, *args, optimized=False, no_site=False):
        flags = (["-O"] if optimized else []) + (["-S"] if no_site else [])
        return subprocess.run(
            [sys.executable, *flags, str(SCRIPT), *args],
            capture_output=True,
            text=True,
            timeout=10,
        )

    def test_import_is_inert_and_pins_exact_uploads(self):
        self.assertEqual(len(audit.EXPECTED), 2)
        self.assertEqual(
            audit.EXPECTED["Pay Load Patcher.py"],
            "9b5f05aa2897ae94f1a22b28af838f1ac53db35312dddc2b738b469d23ee6832",
        )
        self.assertEqual(
            audit.EXPECTED["Pay Load Patcher.js"],
            "9de47ff88e6201347df7d342fb16159bd915b370959cad1f820d04449ee03916",
        )
        self.assertNotIn("REPORT", vars(audit), "import must not start an audit")
        for digest in audit.EXPECTED.values():
            self.assertEqual(len(bytes.fromhex(digest)), 32)

    def test_help_needs_neither_pefile_nor_payload(self):
        result = self.invoke("--help", no_site=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("NOT Windows runtime proof", result.stdout)
        self.assertIn("--reference", result.stdout)

    def test_optimized_python_refused(self):
        result = self.invoke("--source-dir", "unused", optimized=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("requires its assertions", result.stderr)

    def test_existing_report_is_never_clobbered(self):
        with tempfile.TemporaryDirectory() as temp:
            report = pathlib.Path(temp) / "keep.json"
            report.write_text("keep this report")
            result = self.invoke("--source-dir", temp, "--json", str(report), no_site=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn("report already exists", result.stderr)
            self.assertEqual(report.read_text(), "keep this report")

    def test_changed_sources_are_not_executed_or_overwritten(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            marker = root / "must-not-exist"
            source = (
                "from pathlib import Path\n"
                + "Path("
                + repr(str(marker))
                + ").write_text('should never run')\n"
            )
            py = root / "Pay Load Patcher.py"
            js = root / "Pay Load Patcher.js"
            py.write_text(source)
            js.write_text("throw new Error('should never run');\n")
            before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (py, js)}
            result = self.invoke("--source-dir", str(root), "--json", str(root / "new.json"), no_site=True)
            self.assertEqual(result.returncode, 2)
            # Missing Node is also a pre-execution refusal on minimal hosts.
            self.assertTrue("Source hash mismatch" in result.stderr or "Node.js is required" in result.stderr)
            self.assertFalse(marker.exists())
            self.assertFalse((root / "new.json").exists())
            self.assertEqual(before, {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (py, js)})

    def test_synthetic_pe_layout_is_data_only(self):
        for dll in (False, True):
            data = audit.fixture(dll=dll)
            self.assertEqual(len(data), 0xA00)
            self.assertEqual(data[:2], b"MZ")
            self.assertEqual(data[0x80:0x84], b"PE\0\0")
            self.assertEqual(struct.unpack_from("<H", data, 0x84)[0], 0x8664)
            self.assertEqual(struct.unpack_from("<H", data, 0x98)[0], 0x20B)
            self.assertEqual(struct.unpack_from("<H", data, 0x86)[0], 2)
            self.assertEqual(bool(struct.unpack_from("<H", data, 0x96)[0] & 0x2000), dll)
        exported = audit.fixture(dll=True)
        self.assertIn(b"zlag_init\0other_init\0", exported)
        self.assertIn(b"KERNEL32.dll\0", audit.fixture())


if __name__ == "__main__":
    unittest.main(verbosity=2)
