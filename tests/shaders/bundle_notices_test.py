"""Keep complete installed notices exact and each raw token within MSVC's limit."""
import hashlib
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
source = (root / "src/assets/shader-notices.inc").read_text(encoding="utf-8")
# MSVC v143 rejects a single >16 KiB raw token. Adjacent smaller literals are
# concatenated by C++ without adding whitespace to the installed notice file.
pattern = re.compile(r'R"BSINotices\((.*?)\)BSINotices"', re.S)
chunks = pattern.findall(source)
assert chunks and not pattern.sub("", source).strip(), "Only adjacent bounded raw literals are allowed"
assert all(len(chunk.encode("utf-8")) <= 4096 for chunk in chunks), "Keep raw tokens below MSVC's limit"
notice = "".join(chunks)
assert len(notice.encode()) == 17709 and len(notice.encode()) < 32768
assert hashlib.sha256(notice.encode()).hexdigest() == "fd42573ba8962381dbf5f27419147cc33f454bc1b97e4134113ef9102554fd9c", "Installed ownership/license bytes must not change when splitting literals"
for name in (
    "src/third_party/minhook/LICENSE.txt",
    "src/third_party/vulkan/LICENSE.md",
    "src/third_party/vulkan/LICENSES/MIT.txt",
    "src/third_party/vulkan/LICENSES/Apache-2.0.txt",
):
    original = (root / name).read_text(encoding="utf-8-sig").strip()
    assert original and original in notice, f"Missing/changed complete upstream notice: {name}"
print("PASS: complete MinHook/HDE and Vulkan MIT/Apache notices, exact upstream/installed bytes, bounded MSVC raw tokens and installed file")
