#!/usr/bin/env python3
"""Compact the complete inline sidebar without changing native logic.
Only the BSI visual component's icon table/resolver and outside-literal indentation
are reduced. Native functions, model, popup components and flags are not removed.
"""
import argparse
import json
from pathlib import Path
import re
from build_external import structure, mask_qml, qcompress

SIDEBAR_ICONS = {'fullscreen','volume','mute','keymap','memory','install','more',
                 'settings','back','home','recent'}


def compact(source):
    code, pairs, _ = structure(source)
    helper = re.search(r'component\s+BsiSidebarInlineVisual\s*:\s*Item\s*{', code)
    if not helper or 'BSI SIDEBAR INLINE V1' not in source:
        raise ValueError('Expected the complete BSI inline sidebar; refusing an unknown resource.')
    start = helper.start()
    end = pairs[code.index('{', start)] + 1
    visual = source[start:end]
    table = re.search(r'readonly property var iconShapes:\s*\((\{.*?\})\)', visual, re.S)
    if not table:
        raise ValueError('Unrecognized embedded icon table.')
    shapes = json.loads(table[1])
    if not SIDEBAR_ICONS.issubset(shapes):
        raise ValueError('Missing required sidebar icon drawings.')
    visual = (visual[:table.start(1)] +
              json.dumps({k:v for k,v in shapes.items() if k in SIDEBAR_ICONS},
                         ensure_ascii=False, separators=(',',':')) + visual[table.end(1):])
    vcode, vpairs, _ = structure(visual)
    resolver = re.search(r'function resolveIcon\(\)\s*{', vcode)
    if not resolver:
        raise ValueError('Unrecognized BSI icon resolver.')
    resolver_end = vpairs[vcode.index('{', resolver.start())] + 1
    visual = visual[:resolver.start()] + '''function resolveIcon() {
        var id = elementId.toLowerCase().replace(/[^a-z0-9]/g, "");
        if (id === "volume") return assetName.toLowerCase().indexOf("mute") >= 0 ? "mute" : "volume";
        var names = {fullscreen:"fullscreen",gamecontrol:"keymap",memorytrim:"memory",installapk:"install",more:"more",settings:"settings",back:"back",home:"home",recent:"recent"};
        return names[id] || "";
    }''' + visual[resolver_end:]
    output = source[:start] + visual + source[end:]
    # Keep all newlines (JS automatic-semicolon-insertion behavior), comments and
    # quoted data intact. Trim indentation only if the first non-space character
    # is code, not part of a string, regex literal or comment. No JS minification.
    masked = mask_qml(output)
    rows = []
    for raw, plain in zip(output.splitlines(keepends=True), masked.splitlines(keepends=True)):
        index = len(raw) - len(raw.lstrip(' \t'))
        rows.append(raw[index:] if index < len(plain) and not plain[index].isspace() else raw)
    output = ''.join(rows)
    output = output.replace('BSI SIDEBAR INLINE V1', 'BSI SIDEBAR INLINE V1 COMPACT', 1)
    structure(output)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--capacity', type=int, required=True)
    args = parser.parse_args()
    output = compact(args.input.read_text(encoding='utf-8'))
    payload = len(qcompress(output.encode('utf-8')))
    if args.capacity <= 0 or payload > args.capacity:
        parser.exit(1, f'Refused: {payload} bytes exceed capacity {args.capacity}. Nothing written.\n')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x', encoding='utf-8', newline='') as file:
        file.write(output)
    print(f'Created {args.output}: {payload}/{args.capacity} qCompress bytes. No EXE written.')


if __name__ == '__main__':
    main()
