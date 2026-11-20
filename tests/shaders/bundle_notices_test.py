"""Keep the installed shader module's complete binary-redistribution notices exact."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
notice = (root / "src/assets/shader-notices.inc").read_text(encoding="utf-8")
assert notice.startswith('R"BSINotices(') and notice.endswith(')BSINotices"\n')
assert len(notice.encode()) < 32768
for name in (
    "src/third_party/minhook/LICENSE.txt",
    "src/third_party/vulkan/LICENSE.md",
    "src/third_party/vulkan/LICENSES/MIT.txt",
    "src/third_party/vulkan/LICENSES/Apache-2.0.txt",
):
    original = (root / name).read_text(encoding="utf-8-sig").strip()
    assert original and original in notice, f"Missing/changed complete upstream notice: {name}"
print("PASS: complete MinHook/HDE and Vulkan MIT/Apache binary-redistribution notices, exact upstream contents, bounded installed notice file")
