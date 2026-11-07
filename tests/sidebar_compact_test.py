"""Compact inline budget and lexical-preservation checks; no native imports needed."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/sidebar-panel'))
sys.path.insert(0, str(ROOT/'tests'))
from build_external import mask_qml, qcompress
from build_inline import restyle
from compact_inline import compact
from sidebar_inline_test import native_bodies


def lexical(text):
    protected = []
    masked = mask_qml(text, protected)
    return re.sub(r'\s+', '', masked), protected, text.count('\n')


def preserved(original, output):
    a, b = native_bodies(original), native_bodies(output)
    assert len(a) == len(b)
    assert [lexical(x) for x in a] == [lexical(x) for x in b], 'Native code/literal/regex/comment/line breaks changed'
    first = 'property list<QtObject> pAllSidebarItems'
    def model(qml):
        start = qml.index(first)
        end = qml.index('property ', qml.index(']\n', start) + 2)
        return qml[start:end]
    assert lexical(model(original)) == lexical(model(output)), 'Native model or feature flags changed'
    assert 'file:///' not in output and 'Icons.js' not in output
    assert output.count('MouseArea {') == original.count('MouseArea {')
    assert 'BSI SIDEBAR INLINE V1 COMPACT' in output
    return len(a)

fixture = (ROOT/'tests/fixtures/sidebar/NativeSidebar.qml').read_text()
inline, _ = restyle(fixture)
reduced = compact(inline)
preserved(fixture, reduced)
# Prove whitespace inside a multiline quoted value, comment and regex is kept.
fixture_with_literals = inline.replace('property bool pIsMuted: false', '''property string untouched: "first
   second"
    /* a comment
       with indentation */
    property bool pIsMuted: false''')
reduced_literals = compact(fixture_with_literals)
assert '"first\n   second"' in reduced_literals
assert '/* a comment\n       with indentation */' in reduced_literals
assert len(qcompress(reduced.encode())) < len(qcompress(inline.encode()))

complete = ROOT/'downloads/Sidebar-Compact/PlrSidebar.qml'
original = ROOT/'downloads/Sidebar-Direct/PlrSidebar.qml'
if complete.exists() and original.exists():
    before, after = original.read_text(), complete.read_text()
    assert compact(before) == after
    count = preserved(before, after)
    size = len(qcompress(after.encode('utf-8')))
    assert size <= 15868, (size, 15868)
    print(f'PASS: complete delivery {size}/15868 bytes, {15868-size} spare; '
          f'{count} native bodies + model/literals/regexes/comments/line breaks preserved')
print('PASS: compact skin reduces visual data/indentation only; no native handlers/features removed')
