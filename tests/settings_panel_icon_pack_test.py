"""Qt-free checks for the supplied Settings-tab pack and its resource budget.

The C++ self-test exercises the actual catalog loader, Apply, SVG rasterization,
custom overrides and restore. This small test additionally checks packaging and
all twelve native page identities without needing a player or Qt installation.
"""
import hashlib
import json
import re
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
pack = repo / "src/assets/settings-panel-icons"
manifest = json.loads((pack / "default-icons.json").read_text())
assert manifest["version"] == 1
assert manifest["sourceSha256"] == "e2dba9b369d058200b7f73c877bfd5c62e4eaf61def34899c35f8760474cf4c7"
expected = {
    "uiperformancesetting.qml": "performance.svg",
    "uidisplaysetting.qml": "display.svg",
    "uigraphicssetting.qml": "graphics.svg",
    "plrgamesetting.qml": "game-settings.svg",
    "plraudiosetting.qml": "devices.svg",
    "plrgamepadsetting.qml": "game-settings.svg",
    "plrpreferencessetting.qml": "preferences.svg",
    "plrdevicesetting.qml": "phone.svg",
    "plrshortcutssetting.qml": "shortcuts.svg",
    "plruserdatasetting.qml": "user-data.svg",
    "plradvancedsetting.qml": "advanced.svg",
    "plraboutsetting.qml": "about.svg",
}
pages = manifest["pages"]
assert all(pages[page] == icon for page, icon in expected.items())
assert len(set(expected.values())) == 11
assert all(page == page.casefold() and "/" not in page for page in pages)
assert all(Path(icon).name == icon and icon.endswith(".svg") for icon in pages.values())
files = set(pages.values())
assert files == {path.name for path in pack.glob("*.svg")}
for name in files:
    svg = (pack / name).read_bytes()
    root = ET.fromstring(svg)
    assert 0 < len(svg) <= 48 * 1024
    assert root.tag == "{http://www.w3.org/2000/svg}svg"
    assert root.attrib["viewBox"] == "0 0 24 24"
    assert b"currentColor" in svg, "keep the supplied source artwork unchanged"
    assert not re.search(rb"<\s*(?:script|foreignObject)\b|\bon[a-z]+\s*=", svg, re.I)

cmake = (repo / "CMakeLists.txt").read_text()
resources = re.search(r'qt_add_resources\(BSICustomizer "settings_panel_icons"(.*?)\n\)', cmake, re.S)
assert resources, "the defaults must be embedded for both native and Chromium builds"
assert 'PREFIX "/settings-panel-icons"' in resources.group(1)
for name in files | {"default-icons.json"}:
    assert f"src/assets/settings-panel-icons/{name}" in resources.group(1)
assert "if(BSI_CHROMIUM_UI)" not in resources.group(0)
print("PASS: supplied pack embeds eleven self-contained SVGs and maps all twelve native Settings tabs")

cpp = (repo / "src/core/SettingsPanelPatcher.cpp").read_text()
window = re.search(r'(?:const )?QString replacement = QStringLiteral\(R"QML\((.*?)\)QML"\);', cpp, re.S)
about = re.search(r'QString generated = QStringLiteral\(R"QML\((.*?)\)QML"\);', cpp, re.S)
assert window and about
# A coarse template-bloat guard only. The full compressed native prefix,
# callbacks and geometry helper are measured in settings_panel_window_budget_test.
assert len(window.group(1).encode()) <= 1762, "keep the shell compact; also run the complete-window compression budget test"
assert "QVariantMap storedIcons = packIcons;" in cpp
assert "storedIcons.insert(it.key().toCaseFolded(), it.value());" in cpp
assert "const bool mustReference = qtMajor != 6 || !packIcons.isEmpty();" in cpp
assert "runtimeAssetPng(it.value().toMap(), true)" in cpp
assert "byFile = byTabIndex(byFile, current.tabs);" in cpp

# A conservative complete-pack payload: independent image hashes for every tab,
# an encoded Windows username, a long installation/data prefix, and four links.
# Count qCompress's four-byte uncompressed-length header, not just raw zlib.
root = ("file:///C:/Users/Tester%20%23%25%20%E8%B7%AF%E5%BE%84/"
        + "Long%20App%20Data%20Path/" * 6
        + "AppData/Local/BSICustomizer/settings-panel-assets/"
        + hashlib.sha256(b"canonical-exe-path").hexdigest() + "/")
icons = {str(i): root + hashlib.sha256(str(i).encode()).hexdigest()[:24] + ".png"
         for i in range(12)}
links = "\n".join(
    "        { title: " + json.dumps(title) + ", url: " + json.dumps(url) + " },"
    for title, url in [
        ("GitHub", "https://github.com/example/project"),
        ("Discord", "https://discord.gg/example"),
        ("YouTube", "https://youtube.com/@example"),
        ("Website", "https://example.com/customizer"),
    ])
values = {
    "DEVELOPER": json.dumps("Mr.Pc Gamer & Romulos"),
    "TAGLINE": json.dumps("BlueStacks tools · with contributor Nova", ensure_ascii=False),
    "HIDDEN_TABS": "[]",
    "TAB_ICONS": "(" + json.dumps(icons, separators=(",", ":")) + ")",
    "LINKS": links,
    "LOGO_SOURCE": json.dumps(root + hashlib.sha256(b"logo").hexdigest()[:24] + ".png"),
}
doc = about.group(1)
for token, value in values.items():
    doc = doc.replace("@@BSI_" + token + "@@", value)
assert "@@BSI_" not in doc
payload = 4 + len(zlib.compress(doc.encode(), 9))
assert payload <= 2337, f"all twelve default icons, logo and links must fit the known About slot: {payload} > 2337"
print(f"PASS: complete 12-tab referenced-artwork payload fits the fixed About slot ({payload}/2337 bytes)")
