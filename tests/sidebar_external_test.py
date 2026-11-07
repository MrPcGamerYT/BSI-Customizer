"""Preservation, compression and fail-closed tests; no Qt installation required."""
import importlib.util
from pathlib import Path
import re
import struct
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('sidebar_build', ROOT/'tools/sidebar-panel/build_external.py')
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)
source = (ROOT/'tests/fixtures/sidebar/NativeSidebar.qml').read_text()
output, buttons = build.restyle(source)
assert len(buttons) == 6, buttons
# Compare ALL named native functions and event bodies, not just their names.
def bodies(qml):
    code, pairs, _ = build.structure(qml)
    result = []
    for m in re.finditer(r'\bfunction\s+\w+\s*\([^)]*\)\s*\{|\bon(?:Pressed|Released|PositionChanged)\s*:\s*(?:function\s*\([^)]*\))?\s*\{', code):
        opening = code.index('{', m.start())
        result.append(qml[m.start():pairs[opening]+1])
    return result
assert bodies(output) == bodies(source)
assert 'acceptedButtons: Qt.LeftButton | Qt.RightButton' in output
assert 'property int pSidebarElementSize: 32' in output
assert 'pIsFeatureEnable:false' in output
assert output.count('color: "transparent" // Let the violet rail') == 1
# Exact native model/flags and the complete popup component remain unchanged.
for start, end in [('property list<QtObject>', 'function clicked'), ('Component {', '\n}')]:
    original = source[source.index(start):source.rindex(end) if end == '\n}' else source.index(end)]
    assert original in output
assert 'UiImageButton { id: iPopupButton; asset: "PopupNativeAsset" }' in output
assert 'item.assetName = Qt.binding(function() { return (\nmodel.modelData.pAsset' in output
assert 'item.assetName = Qt.binding(function() { return parent.asset' not in output
# SVG replacements are original images, never a mouse event overlay.
skin = (ROOT/'assets/emulator-sidebar/BSISidebarVisual.qml').read_text()
assert not re.search(r'\b(MouseArea|TapHandler|HoverHandler)\s*{', skin)
assert 'glyph.status === Image.Ready' in skin
assert '.item.known ? ""' in output
for invalid in [output, source.replace('pSidebarElementSize: 32', 'pSidebarElementSize: 48'),
                source.replace('id: iBtn', 'id: unexpectedDelegate'),
                source.replace('asset: model.modelData.pAsset', 'asset: model.modelData.\n pAsset')]:
    try:
        build.restyle(invalid)
    except ValueError:
        pass
    else:
        raise AssertionError('Invalid source was accepted')
for url in ['https://example.com/BSISidebarVisual.qml', 'file://host/share/BSISidebarVisual.qml',
            'file:///relative.qml', 'file:///C:/Assets/BSISidebarVisual.qml?x=1']:
    try:
        build.restyle(source, url)
    except ValueError:
        pass
    else:
        raise AssertionError('Unsafe URL accepted')
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    original = temp/'original.qml'
    original.write_text(source)
    target = temp/'rejected'
    try:
        build.build(original, target, build.DEFAULT_URL, 10)
    except ValueError as e:
        assert 'Nothing written' in str(e)
    else:
        raise AssertionError('Capacity refusal missing')
    assert not target.exists()
    target = temp/'success'
    size = len(build.qcompress(output.encode()))
    build.build(original, target, build.DEFAULT_URL, size)
    assert (target/'Sidebar-Outline.qml').read_text() == output
    payload = build.qcompress(output.encode())
    assert struct.unpack('>I', payload[:4])[0] == len(output.encode())
    assert zlib.decompress(payload[4:]) == output.encode()
    try:
        build.build(original, target, build.DEFAULT_URL, size)
    except ValueError:
        pass
    else:
        raise AssertionError('Existing output overwritten')
    assert original.read_text() == source
print('PASS: native functions, handlers, model/flags, popups, fallback, geometry, safe capacity/no-overwrite')

# Native button components may implement their own input. Preserve them as-is,
# don't add a MouseArea, and observe pressed only if the component exposes it.
no_inline = source.replace('MouseArea { anchors.fill:parent; onPressed: navigationCount++; onReleased: releasedCount++ }',
                           'onHoveredChanged: { /* native component owns input */ }')
no_inline_output, _ = build.restyle(no_inline)
assert no_inline_output.count('MouseArea {') == no_inline.count('MouseArea {')
assert '("pressed" in iBackBtn) ? !!iBackBtn.pressed : false' in no_inline_output
assert '("pressed" in iHomeBtn) ? !!iHomeBtn.pressed : false' in no_inline_output
assert 'onHoveredChanged: { /* native component owns input */ }' in no_inline_output
ambiguous = source.replace('id: iBackBtn', 'id: iBackBtn\n MouseArea { }')
try:
    build.restyle(ambiguous)
except ValueError as e:
    assert 'iBackBtn' in str(e) and '2 direct MouseAreas' in str(e)
else:
    raise AssertionError('Ambiguous mouse areas were silently accepted')
print('PASS: component-owned input preserved, absent pressed is guarded, ambiguous areas identify the button')
