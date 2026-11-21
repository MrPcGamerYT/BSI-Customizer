"""Keep native-test failures and optional timeouts visible in check annotations.

This neither skips drivers/assertions nor turns a timeout into a passing result.
Capturing a bounded diagnostic lets restricted API clients inspect failures
without downloading from a separate Actions log-storage host.
"""
import os
import subprocess
import sys
import tempfile

command = sys.argv[2:] if sys.argv[1:2] == ["--command"] else ["ctest", *sys.argv[1:]]
limit = os.environ.get("BSI_TEST_COMMAND_TIMEOUT")
try:
    timeout = int(limit) if limit is not None else None
    if timeout is not None and not 1 <= timeout <= 3600:
        raise ValueError()
except ValueError:
    print("BSI_TEST_COMMAND_TIMEOUT must be an integer between 1 and 3600 seconds", file=sys.stderr)
    raise SystemExit(2)
# Do not use communicate()/pipe EOF as the process-liveness signal: a Windows
# error reporter/descendant can inherit stdout after the tested parent exits.
# A private temporary file also avoids accumulating a build log in pipe RAM.
with tempfile.TemporaryFile() as log:
    process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
    diagnostic = ""
    try:
        status = process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        status = 124
        process.kill()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            diagnostic = "\nThe timed-out test's termination could not be confirmed; CI cleanup is still required.\n"
        diagnostic += f"\nFAIL: native test command exceeded {timeout} seconds; this is NOT a passing result.\n"
    log.seek(0)
    output = log.read().decode("utf-8", errors="replace") + diagnostic
print(output, end="", flush=True)
if status:
    # The check-annotation API truncates messages at 4096 bytes. Keep the TAIL,
    # including the actual failure, not 24 KiB whose useful end gets discarded.
    tail = output.encode("utf-8")[-3000:].decode("utf-8", errors="replace")
    message = tail.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    print("::error title=Native shader test failure::" + message, flush=True)
raise SystemExit(status)
