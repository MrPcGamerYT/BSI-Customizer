"""The CI wrapper must preserve failures/timeouts and their bounded diagnostics."""
import os
from pathlib import Path
import subprocess
import sys
import unittest

WRAPPER = Path(__file__).with_name("run_ci_tests.py")


def run(source, limit=None):
    env = dict(os.environ)
    env.pop("BSI_TEST_COMMAND_TIMEOUT", None)
    if limit is not None:
        env["BSI_TEST_COMMAND_TIMEOUT"] = limit
    return subprocess.run([sys.executable, str(WRAPPER), "--command", sys.executable, "-u", "-c", source],
                          env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, encoding="utf-8", timeout=15)


class DiagnosticsTests(unittest.TestCase):
    def test_success_not_annotated(self):
        result = run('print("native success")')
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("native success", result.stdout)
        self.assertNotIn("::error", result.stdout)

    def test_failure_preserves_exit_and_escapes_annotation(self):
        result = run('import sys;print("native 100% failure");sys.exit(7)')
        self.assertEqual(result.returncode, 7, result.stdout)
        self.assertIn("native 100% failure", result.stdout)
        self.assertIn("::error title=Native shader test failure::native 100%25 failure%0A", result.stdout)

    def test_timeout_is_failure_with_partial_output(self):
        result = run('import time;print("before blocking", flush=True);time.sleep(5)', "1")
        self.assertEqual(result.returncode, 124, result.stdout)
        self.assertIn("before blocking", result.stdout)
        self.assertIn("exceeded 1 seconds", result.stdout)
        self.assertIn("::error", result.stdout)

    def test_invalid_timeout_never_executes_command(self):
        for value in ("bad", "0", "-1", "3601"):
            with self.subTest(value=value):
                result = run('print("MUST NOT RUN")', value)
                self.assertEqual(result.returncode, 2, result.stdout)
                self.assertNotIn("MUST NOT RUN", result.stdout)


if __name__ == "__main__":
    unittest.main()
