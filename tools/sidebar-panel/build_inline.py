#!/usr/bin/env python3
"""Self-contained right-sidebar visual patch. No external QML/JS/asset URLs.
The native resource writer remains authoritative; this tool never writes an EXE.
"""
import argparse
import json
from pathlib import Path
import re
from build_external import restyle as external_restyle, structure, qcompress

ROOT = (Path(__file__).resolve().parent/'../..').resolve()


def inline_visual():
    template = (ROOT/'assets/emulator-sidebar/BSISidebarVisual.qml').read_text(encoding='utf-8')
    js = (ROOT/'assets/emulator-sidebar/Icons.js').read_text(encoding='utf-8')
    shapes = json.loads(js[js.index('var shapes = ') + len('var shapes = '):].strip().rstrip(';'))
    body = template[template.index('Item {') + len('Item {'):template.rfind('}')]
    body = body.replace('Icons.shapes[iconKey]', 'iconShapes[iconKey]')
    # Complete drawing table embedded in the component: no file:/// or JS import.
    table = '\n    readonly property var iconShapes: (' + json.dumps(shapes, ensure_ascii=False, indent=4) + ')\n'
    return '    component BsiSidebarInlineVisual: Item {' + table + body + '\n    }\n'


def restyle(source):
    output, buttons = external_restyle(source)
    # The external restyler only touches known native visual bindings. Turn its
    # bridge into direct, declarative inline-component instances.
    code, pairs, _ = structure(output)
    edits = []
    for m in re.finditer(r'\bLoader\s*\{', code):
        opening = code.index('{', m.start())
        end = pairs[opening] + 1
        block = output[m.start():end]
        identity = re.search(r'\bid:\s*(bsiSidebarSkin(?:Rail|\d+))\b', block)
        if not identity:
            continue
        name = identity[1]
        if name.endswith('Rail'):
            replacement = ('BsiSidebarInlineVisual { id: ' + name + ';\n'
                           '        objectName: "bsiSidebarInlineRail"\n'
                           '        anchors.fill: parent; mode: "rail" }')
        else:
            fields = re.findall(r'item\.(\w+) = Qt\.binding\(function\(\) \{ return (.*?); \}\);', block, re.S)
            if len(fields) != 7:
                raise ValueError('Unrecognized generated visual binding bridge.')
            replacement = 'BsiSidebarInlineVisual {\n                id: ' + name + '\n                anchors.fill: parent\n'
            for prop, expression in fields:
                expression = expression.replace('item.assetName', name + '.assetName')
                replacement += '                ' + prop + ': ' + expression + '\n'
            replacement += '            }'
        edits.append((m.start(), end, replacement))
    if len(edits) != len(buttons) + 1:
        raise ValueError('Incomplete visual bridge conversion; no output written.')
    for start, end, replacement in sorted(edits, reverse=True):
        output = output[:start] + replacement + output[end:]
    for index in range(len(buttons)):
        name = 'bsiSidebarSkin' + str(index)
        output = output.replace(name + '.status === Loader.Ready && ' + name + '.item.known', name + '.known')
    root = re.search(r'\bRectangle\s*\{', structure(output)[0])
    opening = output.index('{', root.start())
    output = output[:opening + 1] + '\n    // BSI SIDEBAR INLINE V1 — complete visual skin, no external files.\n' + inline_visual() + output[opening + 1:]
    output = output.replace('// BSI sidebar visual bridge; native model and callbacks below are unchanged.',
                            '// Inline visual rail; native model and callbacks below are retained.')
    # Keep native 32px geometry even if a UiImageButton calculates its implicit
    # size from its (now empty) stock asset. Never change the caller's sidebar anchors.
    code, pairs, levels = structure(output)
    edits = []
    for m in re.finditer(r'\bUiImageButton\s*\{', code):
        opening = code.index('{', m.start())
        end = pairs[opening]
        if not re.search(r'\bid:\s*bsiSidebarSkin\d+\b', code[opening:end]):
            continue
        additions = []
        for dimension in ['width', 'height']:
            if not any(levels[opening + 1 + x.start()] == levels[opening] + 1
                       for x in re.finditer(r'(?<![\w.])' + dimension + r'\s*:', code[opening + 1:end])):
                additions.append('            ' + dimension + ': pSidebarElementSize')
        if additions:
            edits.append((opening + 1, '\n' + '\n'.join(additions) + '\n'))
    for offset, value in sorted(edits, reverse=True):
        output = output[:offset] + value + output[offset:]
    if 'file:///' in output or 'Icons.js' in output or '.item.known' in output:
        raise ValueError('Unexpected external dependency in inline output.')
    structure(output)
    return output, buttons


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--capacity', required=True, type=int)
    args = parser.parse_args()
    source = args.input.read_text(encoding='utf-8-sig')
    output, buttons = restyle(source)
    size = len(qcompress(output.encode('utf-8')))
    if size > args.capacity or args.capacity <= 0:
        parser.exit(1, f'Refused: {size} compressed bytes exceed capacity {args.capacity}. Nothing written.\n')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x', encoding='utf-8', newline='') as file:
        file.write(output)
    print(f'Created {args.output}: {len(output.encode())} source bytes, {size} qCompress bytes. '
          f'{len(buttons)} native visual sites. No EXE written; no live emulator verification.')


if __name__ == '__main__':
    main()
