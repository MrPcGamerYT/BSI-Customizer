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


def compact_whitespace(qml):
    # Mirror QmlText::compactWhitespace, including its conservative bailouts.
    if "`" in qml:
        return qml
    masked = mask(qml)
    if "/" in masked:
        return qml
    separators, operators, operator_chars = "(){}[]:;,", "=!<>&|", "=!<>+-*/%&|^~?."
    result, line_start, i = [], True, 0
    while i < len(qml):
        char = qml[i]
        if char in " \t" and masked[i] == char:
            end = i + 1
            while end < len(qml) and qml[end] in " \t" and masked[end] == qml[end]:
                end += 1
            left = masked[i - 1] if i else "\0"
            right = masked[end] if end < len(qml) else "\0"
            beside_separator = left in separators or right in separators
            beside_operator = ((left in operators or right in operators)
                               and not (left in operator_chars and right in operator_chars))
            if not (line_start or beside_separator or beside_operator):
                result.append(qml[i:end])
                line_start = False
            i = end
            continue
        result.append(char)
        line_start = char in "\r\n"
        i += 1
    return "".join(result)


def lexical_tokens(qml):
    # Comments/literals are indivisible. Maximal operators catch accidental
    # token joins (+ + -> ++, = = -> ==), not just equal non-whitespace bytes.
    tokens = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
                        r'|[A-Za-z_$][\w$]*|\d+(?:\.\d+)?'
                        r'|===|!==|==|!=|<=|>=|&&|\|\||\+\+|--|\+=|-=|=>|<<|>>|\?\.'
                        r'|[^\s]', re.S)
    return [re.sub(r"(?m)^[ \t]+", "", t[0]) if t[0].startswith("/") else t[0]
            for t in tokens.finditer(qml)]


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
# Pin the annotated side-gutter request independently of the generated shell.
# Tightening the frame must not stretch the native form or make it square.
size_reference = json.loads((REPO / "tests/fixtures/settings-window-size-reference.json").read_text())
panel, outer = size_reference["opaquePanel"], size_reference["outerWindow"]
for dimension in ("width", "height"):
    assert panel[dimension] + 2 * size_reference["nativeShadowInset"] == outer[dimension]
assert (panel["width"], panel["height"]) == (750, 470)
assert panel["width"] == size_reference["nativePageWidth"] + 2 * size_reference["horizontalPageGutter"]
assert panel["width"] / panel["height"] >= size_reference["minimumWideAspectRatio"]
assert f"width=Math.min(t.width*.96,{outer['width']})" in helper
assert f"height=Math.min(t.height*.94,{outer['height']})" in helper
rebuilt = stock[:start] + shell + stock[end:]
close = rebuilt.rfind("\n}")
assert close >= 0
rebuilt = rebuilt[:close] + helper + rebuilt[close:]
assert "QString out = QmlText::compactWhitespace(stock.left(target.nameBegin));" in CPP, "compact the native prefix as well as keeping the generated shell small"
compact = compact_whitespace(stock[:start]) + shell + stock[end:]
compact_close = compact.rfind("\n}")
compact = compact[:compact_close] + helper + compact[compact_close:]
assert compact_whitespace(compact_whitespace(stock[:start])) == compact_whitespace(stock[:start])
# Every token, callback, string, notice and line ending remains; only redundant
# horizontal whitespace outside strings/comments may disappear.
assert lexical_tokens(rebuilt) == lexical_tokens(compact)
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
for original in (quoted, template, regex, "  Item { width: 100 / 2 }\n",
                 "  QtObject { function f(a, b) { return a + +b - -b } }\r\n",
                 "  Item { property bool x: 1 < < 2; property bool y: 1 = = 2 }\n",
                 "  Item { /* : , =  keep notice  */ property string t: ' = { x } ' }\n"):
    result = compact_whitespace(original)
    assert lexical_tokens(result) == lexical_tokens(original)
    assert result.count("\n") == original.count("\n")
    assert result.count("\r") == original.count("\r")
    assert compact_whitespace(result) == result
print(f"PASS: complete legacy Settings window fits its real qCompress slot ({after}/2468 bytes, was {before}); native callbacks, notices, operator tokens and literal whitespace survive")
