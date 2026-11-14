"""Keep native Qt crash traces visible in CI check annotations as well as logs."""
import os
import subprocess
import sys

result = subprocess.run([sys.executable, "-X", "faulthandler", "-u", sys.argv[1]],
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
print(result.stdout, end="", flush=True)
if result.returncode and os.environ.get("GITHUB_ACTIONS"):
    print(f"::error::Qt runtime child exit status: {result.returncode}", flush=True)
    for line in result.stdout.splitlines()[-70:]:
        escaped = line.replace("%", "%25").replace("\r", "%0D")
        print(f"::error::{escaped}", flush=True)
sys.exit(0 if result.returncode == 0 else 1)
