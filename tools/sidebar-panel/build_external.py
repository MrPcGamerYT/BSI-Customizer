#!/usr/bin/env python3
"""Patch the native right sidebar's visuals, never an EXE or its backend callbacks.
Refuses unknown layouts, existing output, and over-capacity qCompress payloads.
Python 3.9+; standard library only. The QML sidecar needs Qt Quick 2.15 + QtSvg.
"""
import argparse
import json
from pathlib import Path
import re
import struct
from urllib.parse import urlsplit
import zlib

ROOT = (Path(__file__).resolve().parent/'../..').resolve()
DEFAULT_URL = 'file:///C:/Program%20Files/BlueStacks_msi5/Assets/BSISidebarVisual.qml'


def mask_qml(text, protected=None):
    """Keep offsets/newlines; mask comments, quoted text and JS regex literals."""
    out = list(text)
    i = 0
    while i < len(text):
        start = i
        if text.startswith('//', i):
            end = text.find('\n', i)
            i = len(text) if end < 0 else end
        elif text.startswith('/*', i):
            end = text.find('*/', i + 2)
            if end < 0:
                raise ValueError('Unclosed comment.')
            i = end + 2
        elif text[i] in "\"'`":
            quote = text[i]
            i += 1
            while i < len(text):
                if text[i] == '\\':
                    i += 2
                elif text[i] == quote:
                    i += 1
                    break
                else:
                    i += 1
            else:
                raise ValueError('Unclosed string.')
        elif text[i] == '/' and (
            not ''.join(out[:i]).rstrip() or
            ''.join(out[:i]).rstrip()[-1] in '=(:,![&|?;{' or
            re.search(r'\b(return|case)\s*$', ''.join(out[:i]))
        ):
            # A regex can contain braces, escaped slashes, quotes and character classes.
            i += 1
            in_class = False
            while i < len(text):
                ch = text[i]
                if ch in '\r\n':
                    raise ValueError('Unclosed regular expression.')
                if ch == '\\':
                    i += 2
                    continue
                if ch == '[':
                    in_class = True
                elif ch == ']':
                    in_class = False
                elif ch == '/' and not in_class:
                    i += 1
                    while i < len(text) and text[i].isalpha():
                        i += 1
                    break
                i += 1
            else:
                raise ValueError('Unclosed regular expression.')
        else:
            i += 1
            continue
        if protected is not None:
            protected.append(text[start:min(i, len(text))])
        for j in range(start, min(i, len(text))):
            if text[j] not in '\r\n':
                out[j] = ' '
    return ''.join(out)


def structure(source):
    code = mask_qml(source)
    pairs, levels, stack = {}, [], []
    for i, c in enumerate(code):
        levels.append(len(stack))
        if c == '{':
            stack.append(i)
        elif c == '}':
            if not stack:
                raise ValueError('Unbalanced QML braces.')
            pairs[stack.pop()] = i
    if stack:
        raise ValueError('Unbalanced QML braces.')
    return code, pairs, levels


def own_binding(source, code, levels, opening, end, name):
    pattern = r'(?m)^[ \t]*' + re.escape(name) + r'\s*:'
    matches = [m for m in re.finditer(pattern, code[opening + 1:end])
               if levels[opening + 1 + m.start()] == levels[opening] + 1]
    if len(matches) != 1:
        raise ValueError(f'Expected one direct {name} binding in a native button.')
    start = opening + 1 + matches[0].start()
    colon = code.index(':', start)
    line_end = source.find('\n', colon)
    line_end = end if line_end < 0 else min(line_end, end)
    expression = source[colon + 1:line_end].strip().rstrip(';')
    masked = code[colon + 1:line_end].strip().rstrip(';')
    if not expression or expression[-1] in '?+*/|&.,:' or '\r' in expression:
        raise ValueError('Multiline/unsupported asset binding; refusing to guess.')
    # Balanced brackets on this line are required; the rest of the file is not rewritten.
    for left, right in [('(', ')'), ('[', ']'), ('{', '}')]:
        if masked.count(left) != masked.count(right):
            raise ValueError('Multiline asset binding; refusing to guess.')
    return colon + 1, line_end, expression


def restyle(source, asset_url=DEFAULT_URL):
    url = urlsplit(asset_url)
    if url.scheme != 'file' or url.netloc or not url.path.startswith('/') or url.query or url.fragment:
        raise ValueError('Use an absolute local file:/// URL, not a network asset.')
    if not url.path.endswith('/BSISidebarVisual.qml'):
        raise ValueError('Asset URL must end in /BSISidebarVisual.qml.')
    if re.search(r'\bbsiSidebarSkin\w*\b', source):
        raise ValueError('Sidebar already contains the BSI skin; use the original backup.')
    code, pairs, levels = structure(source)
    if not re.search(r'\bpSidebarElementSize\s*:\s*32\b', code):
        raise ValueError('Expected the native 32px sidebar; refusing to change geometry.')
    roots = [m for m in re.finditer(r'\bRectangle\s*\{', code) if levels[code.index('{', m.start())] == 0]
    if len(roots) != 1:
        raise ValueError('Expected one native Rectangle sidebar root.')
    root_open = code.index('{', roots[0].start())
    root_end = pairs[root_open]
    # Match only the main column with the native model. Popup columns are NOT altered.
    columns = []
    for m in re.finditer(r'\bUiColumnLayout\s*\{', code):
        opening = code.index('{', m.start())
        if not root_open < opening < root_end:
            continue
        end = pairs[opening]
        if re.search(r'\bmodel\s*:\s*pSidebarItems\b', code[opening:end]):
            columns.append((opening, end))
    if len(columns) != 1:
        raise ValueError('Expected one main sidebar UiColumnLayout with pSidebarItems.')
    col_open, col_end = columns[0]
    if re.search(r'(?m)^\s*color\s*:', code[col_open:col_end]):
        # Only a direct column color is a conflict, not colors in its child objects.
        if any(levels[col_open + m.start()] == levels[col_open] + 1 for m in
               re.finditer(r'(?m)^[ \t]*color\s*:', code[col_open:col_end])):
            raise ValueError('Main column already has a color; refusing to overwrite it.')
    edits = [(root_open + 1, root_open + 1, '\n    // BSI sidebar visual bridge; native model and callbacks below are unchanged.\n'
              '    Loader { id: bsiSidebarSkinRail; anchors.fill: parent;\n'
              '        source: ' + json.dumps(asset_url) + '; onLoaded: item.mode = "rail" }\n'),
             (col_open + 1, col_open + 1, '\n        color: "transparent" // Let the violet rail show through.\n')]
    buttons = []
    fixed = {'iKebabMenuBtn':'more', 'iDiscordBtn':'', 'iChatBtn':'', 'iSettingsBtn':'settings',
             'iBackBtn':'back', 'iHomeBtn':'home', 'iRecentBtn':'recent', 'iRecentAppsBtn':'recent'}
    repeaters = 0
    for m in re.finditer(r'\bUiImageButton\s*\{', code[col_open:col_end]):
        opening = code.index('{', col_open + m.start())
        end = pairs[opening]
        _, _, native_id = own_binding(source, code, levels, opening, end, 'id') if any(
            levels[opening + 1 + x.start()] == levels[opening] + 1
            for x in re.finditer(r'(?m)^[ \t]*id\s*:', code[opening+1:end])) else (0,0,'')
        native_id = native_id.strip()
        # The settings button can be anonymous inside the native iSettingsBtn wrapper.
        is_settings = any(
            levels[p + 1 + hit.start()] == levels[p] + 1
            for p, e in pairs.items() if col_open < p < opening < end < e < col_end
            for hit in re.finditer(r'(?m)^[ \t]*id\s*:\s*iSettingsBtn\b', code[p+1:e]))
        if native_id != 'iBtn' and native_id not in fixed and not is_settings:
            raise ValueError(f'Unrecognized main toolbar button {native_id!r}; no output written.')
        a, b, expression = own_binding(source, code, levels, opening, end, 'asset')
        serial = len(buttons)
        skin_id = 'bsiSidebarSkin' + str(serial)
        if not native_id:
            native_id = 'bsiSidebarSkinButton' + str(serial)
            edits.append((opening + 1, opening + 1, '\n            id: ' + native_id + '\n'))
        if native_id == 'iBtn':
            repeaters += 1
            if not re.fullmatch(r'model\.modelData\.pAsset', expression):
                raise ValueError('Expected the native model.modelData.pAsset delegate binding.')
            element = 'model.modelData.pElementId'
            animating = 'model.modelData.pRunImageAnimation'
        else:
            element = json.dumps('settings' if is_settings else fixed.get(native_id, ''))
            animating = 'false'
        # Track the existing event area for visual press feedback only. No handler changes.
        areas = []
        for am in re.finditer(r'\bMouseArea\s*\{', code[opening:end]):
            ao = code.index('{', opening + am.start())
            if levels[ao] == levels[opening] + 1:
                areas.append(ao)
        if len(areas) > 1:
            raise ValueError(f'Button {native_id}: found {len(areas)} direct MouseAreas; '
                             'ambiguous input layout. No output written. Supply the original QML for inspection.')
        # UiImageButton may own its input area in its component definition rather
        # than exposing a MouseArea in this resource. Observe its pressed property
        # when available, otherwise omit press feedback. NEVER invent an input area.
        pressed_expression = ('("pressed" in ' + native_id + ' && typeof ' + native_id + '.pressed === "boolean") ? ' + native_id + '.pressed : false')
        if areas:
            area = areas[0]
            try:
                _, _, area_id = own_binding(source, code, levels, area, pairs[area], 'id')
            except ValueError as e:
                if 'Expected one direct id' not in str(e):
                    raise
                area_id = skin_id + 'Hit'
                edits.append((area + 1, area + 1, '\n                id: ' + area_id + '\n'))
            if not re.fullmatch(r'[a-z]\w*', area_id):
                raise ValueError('Unsupported MouseArea id.')
            pressed_expression = area_id + '.pressed'
        # Keep original expression as an independent binding, not parent.asset (which loops).
        edits.append((a, b, ' ' + skin_id + '.status === Loader.Ready && ' + skin_id + '.item.known ? "" : (\n' + expression + '\n            )'))
        bridge = f'''\n            Loader {{
                id: {skin_id}; anchors.fill: parent
                source: {json.dumps(asset_url)}
                onLoaded: {{
                    item.elementId = Qt.binding(function() {{ return {element}; }});
                    item.assetName = Qt.binding(function() {{ return (\n{expression}\n                    ); }});
                    item.hovered = Qt.binding(function() {{ return {native_id}.hovered; }});
                    item.pressed = Qt.binding(function() {{ return {pressed_expression}; }});
                    item.available = Qt.binding(function() {{ return {native_id}.enabled; }});
                    item.active = Qt.binding(function() {{ return /Active/.test(item.assetName); }});
                    item.animating = Qt.binding(function() {{ return {animating}; }});
                }}
            }}\n'''
        edits.append((opening + 1, opening + 1, bridge))
        buttons.append(native_id)
    if repeaters != 1 or len(buttons) < 2:
        raise ValueError('Expected one native iBtn delegate plus bottom/overflow controls.')
    result = source
    # Stable reverse application: equal-offset insertions are both retained.
    for start, end, value in sorted(edits, key=lambda e:e[0], reverse=True):
        result = result[:start] + value + result[end:]
    return result, buttons


def qcompress(data):
    return struct.pack('>I', len(data)) + zlib.compress(data, 9)


def build(input_path, output_dir, asset_url, capacity):
    if output_dir.exists():
        raise ValueError('Output directory exists; choose a NEW directory.')
    if capacity <= 0:
        raise ValueError('Capacity must be the actual positive byte limit from the resource writer.')
    source = input_path.read_text(encoding='utf-8-sig')
    output, buttons = restyle(source, asset_url)
    payload = qcompress(output.encode('utf-8'))
    if len(payload) > capacity:
        raise ValueError(f'Refused: sidebar needs {len(payload)} bytes, capacity is {capacity}. '
                         'Nothing written. Do not force EXE growth or bypass the writer.')
    # Prefer the self-contained ZIP assets; otherwise use the repository layout.
    assets = Path(__file__).resolve().parent/'Assets'
    if not assets.exists():
        assets = ROOT/'assets/emulator-sidebar'
    for name in ['BSISidebarVisual.qml', 'Icons.js']:
        if not (assets/name).is_file():
            raise ValueError('Missing skin asset: ' + name)
    (output_dir/'Assets').mkdir(parents=True)
    for name in ['BSISidebarVisual.qml', 'Icons.js']:
        (output_dir/'Assets'/name).write_bytes((assets/name).read_bytes())
    (output_dir/'Sidebar-Outline.qml').write_text(output, encoding='utf-8', newline='')
    report = {'compressed_bytes':len(payload), 'capacity_bytes':capacity,
              'original_compressed_bytes':len(qcompress(source.encode('utf-8'))),
              'asset_url':asset_url, 'native_buttons_styled':buttons,
              'live_emulator_verified':False}
    (output_dir/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))
    print('Created full modified sidebar QML. No EXE was opened or written.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path, help='Original extracted emulator SIDEBAR .qml, NOT Settings')
    parser.add_argument('output', type=Path, help='NEW output directory')
    parser.add_argument('--asset-url', default=DEFAULT_URL)
    parser.add_argument('--capacity', required=True, type=int, help='Actual sidebar compressed-byte limit')
    args = parser.parse_args()
    try:
        build(args.input, args.output, args.asset_url, args.capacity)
    except (ValueError, OSError) as e:
        parser.exit(1, str(e)+'\n')


if __name__ == '__main__':
    main()
