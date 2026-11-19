"""Keep failing native-test diagnostics visible through GitHub check annotations.

The test remains failed: this wrapper neither skips missing drivers nor changes
assertions. Logs are echoed and a bounded diagnostic is attached to the check,
so restricted API clients needn't download an external log-storage URL.
"""
import subprocess
import sys

command = sys.argv[2:] if sys.argv[1:2] == ["--command"] else ["ctest", *sys.argv[1:]]
result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
print(result.stdout, end="", flush=True)
if result.returncode:
    message = result.stdout[-24000:].replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    print("::error title=Native shader test failure::" + message, flush=True)
raise SystemExit(result.returncode)
