"""Qt-free regression for the complete compressed legacy Settings-window slot.

A template-byte ceiling missed a real 2508/2468-byte refusal: the untouched
native prefix and compression entropy matter too. Keep the exact read-only
resource (not the EXE) as a tiny fixture and rebuild the entire document.
Windows engine tests additionally exercise the production C++ against the EXE.
"""
import base64
import hashlib
import json
import re
import struct
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
CPP = (REPO / "src/core/SettingsPanelPatcher.cpp").read_text(encoding="utf-8")
FIXTURE = json.loads((REPO / "tests/fixtures/settings-window-msi-v5.9.json").read_text())
PACKED = base64.b64decode(FIXTURE["qCompressBase64"], validate=True)
RAW = zlib.decompress(PACKED[4:])
assert len(PACKED) == FIXTURE["slotBytes"] == 2468
assert len(RAW) == struct.unpack_from(">I", PACKED)[0] == FIXTURE["decodedBytes"]
assert hashlib.sha256(RAW).hexdigest() == FIXTURE["decodedSha256"]


def mask(qml):
    # Same bounded literal/comment scanner as QmlText::mask, keeping offsets.
    tokens = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
    return tokens.sub(lambda m: (" " if m[0].startswith("/") else "@") * len(m[0]), qml)


def compact_indentation(qml):
    # The production utility refuses JS template literals, preserves quoted
    # line continuations and never removes a newline/statement boundary.
    if "`" in qml:
        return qml
    masked = mask(qml)
    if "/" in masked:
        return qml  # Regex/division ambiguity: the native prefix needs neither.
    result, line_start = [], True
    for char, marked in zip(qml, masked):
        if line_start and char in " \t" and marked == char:
            continue
        result.append(char)
        line_start = char in "\r\n"
    return "".join(result)


stock = RAW.decode("utf-8")  # Do not normalise the fixture's original CRLF.
masked = mask(stock)
objects = list(re.finditer(r"\bUiShadowContainer\s*\{", masked))
assert len(objects) == 1
start, end = objects[0].start(), objects[0].end()
depth = 1
while depth:
    depth += (masked[end] == "{") - (masked[end] == "}")
    end += 1
assert "id: iSettingsWindowShadowContainer" in masked[start:end]
shell = re.search(r'QString replacement = QStringLiteral\(R"QML\((.*?)\)QML"\);', CPP, re.S)[1]
helper = re.search(r'const QString helper = QStringLiteral\(R"QML\((.*?)\)QML"\);', CPP, re.S)[1]
rebuilt = stock[:start] + shell + stock[end:]
close = rebuilt.rfind("\n}")
assert close >= 0
rebuilt = rebuilt[:close] + helper + rebuilt[close:]
assert "QString out = QmlText::compactIndentation(stock.left(target.nameBegin));" in CPP, "compact the native prefix as well as keeping the generated shell small"
compact = compact_indentation(stock[:start]) + shell + stock[end:]
compact_close = compact.rfind("\n}")
compact = compact[:compact_close] + helper + compact[compact_close:]
assert compact_indentation(compact_indentation(stock[:start])) == compact_indentation(stock[:start])
# Every token, callback, string, notice and line ending remains; only leading
# horizontal whitespace outside strings may disappear.
assert re.sub(r"(?m)^[ \t]+", "", rebuilt) == re.sub(r"(?m)^[ \t]+", "", compact)
assert compact.count("\n") == rebuilt.count("\n")
assert compact.count("\r") == rebuilt.count("\r")
for token in ("Copyright (C)", "All Rights Reserved", "fChangeSelectedSetting", "fSwitchSettingPanel",
              "fDiscardChanges", "onClosing", "fPlrResetSettingsObjects", "fEnableShortcutsEditingMode"):
    assert token in compact
before = 4 + len(zlib.compress(rebuilt.encode(), 9))
after = 4 + len(zlib.compress(compact.encode(), 9))
assert after <= FIXTURE["slotBytes"] - 32, f"complete legacy Settings window exceeds its budget/headroom: {after}/2468"
assert after < before

# Independent literal-safety examples, mirrored by real C++/QQmlComponent tests.
quoted = ('  import QtQml 2.15\r\n  QtObject {\r\n'
          '    property string value: "  { // not a comment"\r\n'
          '    property string continued: "first\\\r\n      second"\r\n'
          "\tproperty string single: '  preserve spaces'\r\n  }\r\n")
small = compact_indentation(quoted)
assert '"  { // not a comment"' in small
assert '"first\\\r\n      second"' in small
assert "'  preserve spaces'" in small
assert small.endswith("}\r\n")
template = "  QtObject { property string t: `first\n    ${1 + 2} second` }\n"
assert compact_indentation(template) == template
regex = "  QtObject { function f() { return /'/g; } property string t: 'first\\\n    second' }\n"
assert compact_indentation(regex) == regex
assert compact_indentation("  Item { width: 100 / 2 }\n") == "  Item { width: 100 / 2 }\n"
print(f"PASS: complete legacy Settings window fits its real qCompress slot ({after}/2468 bytes, was {before}); native callbacks, notices and literal whitespace survive")
