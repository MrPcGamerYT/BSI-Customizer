"""Keep native-test failures and optional timeouts visible in check annotations.

This neither skips drivers/assertions nor turns a timeout into a passing result.
Capturing a bounded diagnostic lets restricted API clients inspect failures
without downloading from a separate Actions log-storage host.
"""
import os
import subprocess
import sys

command = sys.argv[2:] if sys.argv[1:2] == ["--command"] else ["ctest", *sys.argv[1:]]
limit = os.environ.get("BSI_TEST_COMMAND_TIMEOUT")
try:
    timeout = int(limit) if limit is not None else None
    if timeout is not None and not 1 <= timeout <= 3600:
        raise ValueError()
except ValueError:
    print("BSI_TEST_COMMAND_TIMEOUT must be an integer between 1 and 3600 seconds", file=sys.stderr)
    raise SystemExit(2)
try:
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, encoding="utf-8", errors="replace", timeout=timeout)
    output, status = result.stdout, result.returncode
except subprocess.TimeoutExpired as error:
    # TimeoutExpired.output is bytes even when text=True. subprocess.run kills
    # and waits for the direct test process; these Qt fixtures spawn no players.
    output = error.output or b""
    if isinstance(output, bytes):
        output = output.decode("utf-8", errors="replace")
    output += f"\nFAIL: native test command exceeded {timeout} seconds; this is NOT a passing result.\n"
    status = 124
print(output, end="", flush=True)
if status:
    message = output[-24000:].replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    print("::error title=Native shader test failure::" + message, flush=True)
raise SystemExit(status)
