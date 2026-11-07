#!/usr/bin/env python3
"""Replace ONLY the Settings visual container; never edit an EXE or native callbacks."""
import argparse
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
TEMPLATE = ROOT / 'assets/emulator-settings/OutlinePanel.qml'


def closing_brace(text, opening):
    depth, i = 0, opening
    while i < len(text):
        c = text[i]
        if text.startswith('//', i):
            end = text.find('\n', i + 2)
            i = len(text) if end < 0 else end
            continue
        if text.startswith('/*', i):
            end = text.find('*/', i + 2)
            if end < 0:
                raise ValueError('Unterminated comment; input was not changed.')
            i = end + 2
            continue
        if c in '\'"`':
            quote = c
            i += 1
            while i < len(text):
                if text[i] == '\\':
                    i += 2
                elif text[i] == quote:
                    i += 1
                    break
                else:
                    i += 1
            continue
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ValueError('Unbalanced visual container; input was not changed.')


def restyle_qml(source, template):
    candidates = []
    for match in re.finditer(r'\bUiShadowContainer\s*\{', source):
        end = closing_brace(source, source.index('{', match.start()))
        if re.search(r'\bid\s*:\s*iSettingsWindowShadowContainer\b', source[match.end():end]):
            candidates.append((match.start(), end))
    if len(candidates) != 1:
        raise ValueError('Expected exactly one named Settings shadow container; refusing to guess.')
    start, end = candidates[0]
    return source[:start] + template.strip() + source[end:]


def restyle_generator(source, template):
    pattern = re.compile(r'(const QString replacement = QStringLiteral\(R"QML\()(.*?)(\)QML"\);)', re.S)
    matches = list(pattern.finditer(source))
    if len(matches) != 1 or 'iSettingsWindowShadowContainer' not in matches[0][2]:
        raise ValueError('Unrecognized SettingsPanelPatcher generator; no changes made.')
    if ')QML"' in template or 'BSI Settings v3' not in template:
        raise ValueError('Template is incompatible with the native generator/recovery marker.')
    match = matches[0]
    return source[:match.start(2)] + '\n' + template.strip() + '\n' + source[match.end(2):]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path, help='Extracted UiSettingsWindow.qml OR SettingsPanelPatcher.cpp')
    parser.add_argument('output', type=Path, help='NEW output file; existing files are never overwritten')
    parser.add_argument('--generator', action='store_true', help='Replace the C++ raw QML template instead of extracted QML')
    args = parser.parse_args()
    source = args.input.read_bytes().decode('utf-8')
    template = TEMPLATE.read_text(encoding='utf-8')
    output = restyle_generator(source, template) if args.generator else restyle_qml(source, template)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x', encoding='utf-8', newline='') as file:
        file.write(output)
    print(f'Created {args.output}. Native callbacks outside the visual template were preserved.')


if __name__ == '__main__':
    main()
