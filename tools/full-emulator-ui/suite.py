"""VOID Carbon Suite 07: full eligible native-QML visual pass, using pinned transactions.
No resource scanning occurs on import. Check/Apply/Export are explicit commands.
"""
import argparse
import base64
import hashlib
import io
import json
from pathlib import Path
import re
import sys
import types
import zipfile
import zlib

LEGACY_B85 = b''  # bundled by build.py
ASSETS_B85 = b''  # bundled by build.py
LEGACY_SHA256 = ''
ASSETS_SHA256 = ''
ROOT = Path(__file__).resolve().parents[2]
PREFIX = 'Assets/VOIDReferenceUI/'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def runtime():
    if LEGACY_B85:
        source = zlib.decompress(base64.b85decode(LEGACY_B85))
        if sha(source) != LEGACY_SHA256:
            raise RuntimeError('Pinned transaction engine digest mismatch.')
    else:
        source = (ROOT/'reference/full-emulator-ui/legacy_installer.py').read_bytes()
        info = json.loads((ROOT/'reference/full-emulator-ui/PROVENANCE.json').read_text())
        if sha(source) != info['installerSha256']:
            raise RuntimeError('Pinned transaction engine changed; review required.')
    module = types.ModuleType('void_suite07_native_engine')
    module.__file__ = __file__
    sys.modules[module.__name__] = module
    exec(compile(source, 'pinned-void-ui-engine.py', 'exec'), module.__dict__)
    packed = module.payload()
    if ASSETS_B85:
        data = base64.b85decode(ASSETS_B85)
        if sha(data) != ASSETS_SHA256:
            raise RuntimeError('Suite asset digest mismatch.')
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            assets = {name: archive.read(name) for name in archive.namelist()}
    else:
        base = ROOT/'assets/full-emulator-ui'
        assets = {p.relative_to(base).as_posix(): p.read_bytes() for p in sorted(base.rglob('*')) if p.is_file()}
    design_bytes = assets['design.json']
    profile_path = Path(__file__).resolve().with_name('design.json')
    if ASSETS_B85 and profile_path.exists():
        if profile_path.is_symlink() or not profile_path.is_file() or profile_path.stat().st_size > 16384:
            raise RuntimeError('Design JSON must be a bounded regular file.')
        design_bytes = profile_path.read_bytes()
    design = json.loads(design_bytes)
    assets['design.json'] = design_bytes
    config = configure(design, json.loads(packed['theme.json']))
    assets['Theme.qml'] = theme_source(design, config)
    packed.update({PREFIX+name: value for name, value in assets.items()})
    packed['theme.json'] = (json.dumps(config, indent=2)+'\n').encode('utf-8')
    module.payload = lambda: packed
    original_adapter = module.native_surface_adapter

    def adapter(manager, name, source, url):
        original = original_adapter(manager, name, source, url)
        if original != source or 'VOID NATIVE SURFACE CONTROLS V5.1' in source:
            return original
        if not re.search(r'(?:Popup|Dialog|Window)\.qml$', name) or name == 'UiSettingsWindow.qml':
            return source
        if '// VOID SUITE07 PASSIVE CONTROLS' in source:
            return source
        tokens, pairs = module.scan(manager, source)
        opening = next((i for i, t in enumerate(tokens) if t.text == '{'), None)
        if opening is None or opening < 1:
            raise RuntimeError('Unrecognized native window root: '+name)
        # Do not insert a visual child into a QtObject/Singleton/model/JS root.
        roots = {'Item', 'Rectangle', 'Window', 'ApplicationWindow', 'Dialog', 'Popup',
                 'UiDialog', 'UiMessageDialog', 'UiWindowedPopup', 'UiWindow', 'UiRowLayout', 'UiColumnLayout'}
        if tokens[opening-1].text not in roots:
            return source
        block = '\n// VOID SUITE07 PASSIVE CONTROLS\nLoader { objectName:"voidSuite07Controls"; visible:false; source:'+json.dumps(url+'NativeControlStyle.qml')+'; onLoaded:item.pages=parent.contentItem || parent }\n'
        after = source[:tokens[opening].end]+block+source[tokens[opening].end:]
        manager.require(after.replace(block, '', 1) == source, 'Passive window adapter altered native source: '+name)
        module.scan(manager, after)
        return after

    module.native_surface_adapter = adapter
    original_plan = module.plan_install

    def plan(root, **kwargs):
        kwargs['full_emulator_ui'] = True
        planned, report = original_plan(root, **kwargs)
        rows = {row['name']: dict(row) for row in report.get('native_coverage', [])}
        for row in report['resources']:
            item = rows.setdefault(row['name'], {})
            item.update(row)
            item.setdefault('status', 'changed' if row['changed'] else 'verified')
        report['suite'] = 'VOID Carbon Suite 07'
        report['all_ui_runtime_certified'] = False
        report['coverage'] = sorted(rows.values(), key=lambda row: (row.get('container', ''), row['name']))
        report['asset_files'] = sorted(name for name in packed if name.startswith(PREFIX))
        report['protected_scope'] = ['boot/loading', 'auth/account/legal', 'boot-shared dependencies', 'Qt framework', 'JavaScript business logic', 'Android/game content', 'Windows-native dialogs']
        report['note'] = 'All mapped eligible QML/SVG files are considered. Changed, protected, unchanged and unsupported fields are explicit; source coverage is not an OEM rendering guarantee.'
        return planned, report

    module.plan_install = plan
    return module


def configure(design, original):
    if set(design) != {'format', 'name', 'font', 'radius', 'motion', 'durationMs', 'palette'} or design['format'] != 1:
        raise RuntimeError('Unsupported design schema.')
    if type(design['radius']) is not int or not 0 <= design['radius'] <= 24:
        raise RuntimeError('Corner radius must be an integer from 0 to 24.')
    if type(design['durationMs']) is not int or not 0 <= design['durationMs'] <= 400 or type(design['motion']) is not bool:
        raise RuntimeError('Invalid motion settings.')
    if not isinstance(design['font'], str) or not re.fullmatch(r'[A-Za-z0-9 _-]{1,64}', design['font']):
        raise RuntimeError('Invalid font family. Font files are not installed by this pack.')
    if not isinstance(design['name'], str) or not 1 <= len(design['name']) <= 80:
        raise RuntimeError('Invalid design name.')
    if set(design['palette']) != {'background', 'surface', 'raised', 'border', 'text', 'muted', 'accent'}:
        raise RuntimeError('Invalid palette keys.')
    for value in design['palette'].values():
        if not isinstance(value, str) or not re.fullmatch(r'#[0-9a-fA-F]{6}', value):
            raise RuntimeError('Only six-digit color values are allowed.')
    config = json.loads(json.dumps(original))
    config['name'] = design['name']
    config['palette'].update(design['palette'])
    # Derived semantic colors must be canonical on the FIRST pass, not slowly
    # recolored through literal aliases on each subsequent install/check.
    palette = config['palette']
    palette.update(overlay_black='#99'+palette['background'][1:],
                   overlay_bright='#1f'+palette['text'][1:],
                   overlay_tint='#66'+palette['surface'][1:],
                   overlay_primary='#cc'+palette['background'][1:],
                   accent_transparent='#44'+palette['accent'][1:],
                   accent_hover=palette['text'])
    config['font_family'] = design['font']
    config['corners'].update(enabled=True, radius=design['radius'])
    config['animations'].update(enabled=design['motion'], color_ms=design['durationMs'])
    return config


def theme_source(design, config):
    rows = ['pragma Singleton', 'import QtQuick 2.15', 'QtObject {',
            '    readonly property bool motionEnabled: '+str(design['motion']).lower(),
            '    readonly property int duration: '+str(design['durationMs'] if design['motion'] else 0),
            '    readonly property int corner: '+str(design['radius']),
            '    readonly property string fontFamily: '+json.dumps(design['font'])]
    rows += ['    readonly property color '+key+': '+json.dumps(value) for key, value in config['palette'].items()]
    return ('\n'.join(rows)+'\n}\n').encode('utf-8')


def export_sources(module, root, destination):
    """Read-only preview of the planned mapped resources; no binary redistribution."""
    manager = module.manager(); engine = manager.engine()
    root = engine.canonical_root(root); destination = destination.absolute()
    engine.need(destination != root and root not in destination.resolve().parents, 'Save the source ZIP outside the emulator installation.')
    engine.need(not destination.exists(), 'Source export already exists; choose a new filename.')
    planned, report = module.plan_install(root)
    changed = {change.relative: change.after for change in planned.changes}
    members = {}
    for path in engine.candidate_files(root):
        if path.suffix.lower() == '.exe' and path.name != 'HD-Player.exe':
            continue
        relative = path.relative_to(root).as_posix()
        engine.safe_target(root, relative)
        engine.need(path.stat().st_size <= engine.MAX_FILE, 'Oversized container: '+relative)
        data = changed.get(relative, path.read_bytes())
        for row in engine.resources_in(data, {}, {})[1]:
            if row.name.endswith(('.qml', '.js', '.svg')) and not manager.framework_path(row.path):
                value = engine.unpack_record(data, row.entry, row.flags)
                # Paths come from validated RCC records; additionally flatten behind a digest.
                name = re.sub(r'[^A-Za-z0-9_.-]', '_', row.name)
                key = 'Native/'+sha((relative+row.path).encode())[:16]+'/'+name
                engine.need(len(value) <= engine.MAX_SOURCE, 'Oversized source export.')
                members[key] = value
                engine.need(len(members) <= 4096 and sum(map(len, members.values())) <= 32*1024*1024, 'Source export limit reached.')
    members.update({name: value for name, value in module.payload().items() if name.startswith(PREFIX)})
    members['COVERAGE.json'] = (json.dumps(report, indent=2)+'\n').encode()
    members['README.txt'] = b'PLANNED SOURCE PREVIEW - NOT INSTALLED. Native QML callbacks are retained. No EXE/DLL is included. Do not copy these loose resources over an emulator. Use CHECK-UI and INSTALL-UI for verified native resource/cache transactions. No automatic upload.\n'
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, value in sorted(members.items()):
            archive.writestr(name, value)
    engine.atomic_write(destination, stream.getvalue(), 0o600)
    return report


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    module = runtime()
    print('VOID CARBON SUITE 07 - full eligible native UI, guarded resources and exact recovery')
    if '--export-sources' in argv:
        parser = argparse.ArgumentParser()
        parser.add_argument('--export-sources', type=Path, required=True)
        parser.add_argument('--install-dir', type=Path)
        args = parser.parse_args(argv)
        root = args.install_dir or module.manager().engine().choose_folder()
        export_sources(module, root, args.export_sources)
        print('READ-ONLY planned source ZIP:', args.export_sources)
        return 0
    if '--full-emulator-ui' not in argv:
        argv.append('--full-emulator-ui')
    return module.main(argv)


if __name__ == '__main__':
    status = 0
    try:
        status = main()
    except (RuntimeError, OSError, ValueError, UnicodeError, KeyError, TypeError, zlib.error) as error:
        print('STOPPED:', error)
        status = 1
    if '--pause' in sys.argv:
        input('Press Enter to close...')
    raise SystemExit(status)
