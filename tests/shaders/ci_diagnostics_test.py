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

    def test_long_log_keeps_actual_failure_in_the_api_annotation(self):
        result = run('import sys;print("x" * 12000);print("FINAL FAILURE");sys.exit(9)')
        self.assertEqual(result.returncode, 9, result.stdout)
        annotation = result.stdout[result.stdout.index("::error"):]
        self.assertIn("FINAL FAILURE", annotation)
        self.assertLess(len(annotation.encode("utf-8")), 4096)

    def test_exited_parent_is_not_kept_alive_by_inherited_stdout(self):
        # The finite child models an inherited error-reporting handle, not a
        # running player. Waiting for pipe EOF would falsely time out its parent.
        result = run('import subprocess,sys;subprocess.Popen([sys.executable,"-c","import time;time.sleep(2)"]);print("PARENT EXITED")', "1")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("PARENT EXITED", result.stdout)

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
