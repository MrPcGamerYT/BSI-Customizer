"""Inline visual integration/preservation tests. No emulator or Qt required."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/sidebar-panel'))
import build_inline
from build_external import structure, qcompress


def native_bodies(source):
    code, pairs, _ = structure(source)
    helper = re.search(r'component\s+BsiSidebarInlineVisual\s*:\s*Item\s*{', code)
    helper_range = (helper.start(), pairs[code.index('{', helper.start())]) if helper else (-1,-1)
    result = []
    for match in re.finditer(r'\bfunction\s+\w+\s*\([^)]*\)\s*{|\bon\w+\s*:\s*(?:function\s*\([^)]*\))?\s*{', code):
        if helper_range[0] < match.start() < helper_range[1]:
            continue
        opening = code.index('{', match.start())
        result.append(source[match.start():pairs[opening]+1])
    return result


def verify(source, output):
    assert native_bodies(source) == native_bodies(output), 'Native functions/handlers changed'
    assert 'component BsiSidebarInlineVisual: Item' in output
    assert 'BSI SIDEBAR INLINE V1' in output
    assert 'file:///' not in output and 'Icons.js' not in output and 'item.assetName' not in output
    assert 'acceptedButtons: ' in output
    assert '.known ? ""' in output
    assert output.count('MouseArea {') == source.count('MouseArea {')
    if 'property list<QtObject> pAllSidebarItems' in source:
        start = source.index('property list<QtObject> pAllSidebarItems')
        # Native fixture uses pSidebarItems as var; the production resource uses list.
        end = source.index('property ', source.index(']\n', start) + 2)
        assert source[start:end] in output, 'Native model/feature flags were changed'
    return len(native_bodies(source))

fixture = (ROOT/'tests/fixtures/sidebar/NativeSidebar.qml').read_text()
styled, buttons = build_inline.restyle(fixture)
verify(fixture, styled)
assert len(buttons) == 6
try:
    build_inline.restyle(styled)
except ValueError:
    pass
else:
    raise AssertionError('Inline skin could be applied twice')
# The optional source is the user's reconstructed pasted resource, kept in cache.
original = ROOT/'.cache/sidebar-direct/Original.qml'
complete = ROOT/'downloads/Sidebar-Direct/PlrSidebar.qml'
if original.exists() and complete.exists():
    source = original.read_text()
    output = complete.read_text()
    assert build_inline.restyle(source)[0] == output
    count = verify(source, output)
    print(f'PASS: complete user-derived resource, {count} native function/handler bodies unchanged; '
          f'{len(output.encode())} source bytes / {len(qcompress(output.encode()))} qCompress bytes')
print('PASS: self-contained inline component; model/flags/events kept; no external paths or extra input handlers')
