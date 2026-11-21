"""VOID OLED Studio 09: full eligible native-QML visual pass, using pinned transactions.
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
# A bundled release may live directly below a drive root (e.g. D:\VOID-UI).
ROOT = Path(__file__).resolve().parent if LEGACY_B85 else Path(__file__).resolve().parents[2]
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
    # Extend the pinned planner AFTER native state/source checks and BEFORE cache,
    # binary and state generation. This also removes additions on tracked upgrades.
    anchor = "    accepted={name:[e.source_identity(source,name)] for name,source in current.items()}"
    text = source.decode('utf-8')
    if text.count(anchor) != 1:
        raise RuntimeError('Pinned planner hook changed; review required.')
    text = text.replace(anchor, "    desired = remove_optimizer(desired, url)\n"+anchor, 1)
    hooks = {
        "    state=m.read_json(state_before) if state_before else {}":
            "    state={} if replacing_ui else (m.read_json(state_before) if state_before else {})",
        "    migration_sources={};migration_info=None":
            "    if replacing_ui: state=replacement_state(root,current,owners,url)\n    migration_sources={};migration_info=None",
        "not known and (name not in CORE_NAMES or not state)":
            "not known and (name not in CORE_NAMES or not state or replacing_ui)",
        "    parser.add_argument('--install-dir',type=Path)":
            "    parser.add_argument('--replace-ui',action='store_true',help='Replace previous UI files/state, retaining a complete undo backup.')\n    parser.add_argument('--install-dir',type=Path)",
        "word='MIGRATE' if args.migrate_reviewed_legacy else 'APPLY'":
            "word='REPLACE' if args.replace_ui else ('MIGRATE' if args.migrate_reviewed_legacy else 'APPLY')",
    }
    for before, after in hooks.items():
        if text.count(before) != 1:
            raise RuntimeError('Pinned replacement hook changed; review required.')
        text = text.replace(before, after, 1)
    text = text.replace('Settings now uses the reference shell and contains the VOID Optimizer tab.', 'Settings now uses OLED Studio with live Appearance controls (no optimizer).')
    text=text.replace('VOID FULL UI V5 — Carbon palette / incremental styling / native session tools; boot logic protected','VOID OLED STUDIO 09 - six dark presets / native UI / no optimizer')
    exec(compile(text, 'pinned-void-ui-engine.py', 'exec'), module.__dict__)
    module.live_names=set()
    module.live_coverage=[]
    module.remove_optimizer = lambda desired, url: live_theme_sources(module, without_optimizer(module, desired, url), url)
    module.replacing_ui = False
    module.replacement_state = lambda root, current, owners, url: replacement_state(module, root, current, owners, url)
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
    validate_presets(json.loads(assets['themes.json']))
    assets['Theme.qml'] = theme_source(design, config, assets['ThemeRuntime.qml'], json.loads(assets['themes.json']))
    module.design_config=config
    packed = {name: value for name, value in packed.items() if not name.startswith(PREFIX)}
    packed.update({PREFIX+name: value for name, value in assets.items()})
    packed['theme.json'] = (json.dumps(config, indent=2)+'\n').encode('utf-8')
    module.payload = lambda: packed
    original_adapter = module.native_surface_adapter

    def adapter(manager, name, source, url):
        original = original_adapter(manager, name, source, url)
        if name == 'KmmEditorWindow.qml':
            original = original.replace('onLoaded:item.pages=parent }', 'onLoaded:{ item.stylePanels=true; item.pages=parent; item.registerNativeWindow=true } }')
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
        block = '\n// VOID SUITE07 PASSIVE CONTROLS\nLoader { objectName:"voidSuite07Controls"; visible:false; source:'+json.dumps(url+'NativeControlStyle.qml')+'; onLoaded:{ item.stylePanels=true; item.pages=parent } }\n'
        after = source[:tokens[opening].end]+block+source[tokens[opening].end:]
        manager.require(after.replace(block, '', 1) == source, 'Passive window adapter altered native source: '+name)
        module.scan(manager, after)
        return after

    module.native_surface_adapter = adapter
    original_inventory = module.full_ui_inventory
    original_style = module.style_full_source
    shared_surfaces = {'UiDialog.qml', 'UiWindowedPopup.qml'}
    def inventory(manager, engine, images, root, current, owners):
        candidates, config, coverage = original_inventory(manager, engine, images, root, current, owners)
        for name in shared_surfaces:
            matches=[]
            for path, (body, records) in images.items():
                for row in records:
                    if row.name == name and not manager.framework_path(row.path):
                        matches.append((path,row,engine.unpack_record(body,row.entry,row.flags)))
            if not matches:
                continue
            engine.need(len(matches)==1, 'Ambiguous shared visual host: '+name)
            path,row,value=matches[0]
            key=path.relative_to(root).as_posix()+'/'+row.path
            candidates[name]=(value,key); current[name]=value; owners[name]=path
            coverage=[entry for entry in coverage if entry['name']!=name]
            coverage.append({'name':name,'container':path.relative_to(root).as_posix(),
                             'status':'scoped-hook','reason':'Only a passive opt-in visual loader; original shared source, boot behavior and callbacks retained.'})
        module.live_names=set(candidates).difference(shared_surfaces) | set(module.RESOURCE_NAMES)
        return candidates,config,coverage
    def style(manager, name, source, path, config, url):
        if name in shared_surfaces:
            return scoped_surface_hook(module, manager, name, source, url), ['registered-native-window-only visual hook'], []
        return original_style(manager,name,source,path,config,url)
    module.full_ui_inventory=inventory
    module.style_full_source=style
    original_plan = module.plan_install

    def plan(root, **kwargs):
        kwargs['full_emulator_ui'] = True
        replace = kwargs.pop('replace_existing_ui', module.replacing_ui)
        previous_mode = module.replacing_ui
        module.replacing_ui = replace
        try:
            planned, report = original_plan(root, **kwargs)
            if replace:
                retire_old_assets(module, planned, packed)
        finally:
            module.replacing_ui = previous_mode
        report['replacement_install'] = replace
        report['changed_files'] = [change.relative for change in planned.changes]
        report['optimizer_included'] = False
        rows = {row['name']: dict(row) for row in report.get('native_coverage', [])}
        for row in report['resources']:
            item = rows.setdefault(row['name'], {})
            item.update(row)
            item.setdefault('status', 'changed' if row['changed'] else 'verified')
        report['suite'] = 'VOID OLED Studio 09'
        report['live_theme_bindings']=module.live_coverage
        report['theme_presets']=[entry['name'] for entry in json.loads(assets['themes.json'])]
        report['all_ui_runtime_certified'] = False
        report['coverage'] = sorted(rows.values(), key=lambda row: (row.get('container', ''), row['name']))
        report['asset_files'] = sorted(name for name in packed if name.startswith(PREFIX))
        report['protected_scope'] = ['boot/loading', 'auth/account/legal', 'boot-shared visual behavior (two opt-in native-host hooks only)', 'Qt framework', 'JavaScript business logic', 'Android/game content', 'Windows-native dialogs']
        report['note'] = 'All mapped eligible QML/SVG files are considered. Changed, protected, unchanged and unsupported fields are explicit; source coverage is not an OEM rendering guarantee.'
        return planned, report

    module.plan_install = plan
    engine = module.manager().engine()
    original_commit = engine.commit
    engine.atomic_write = atomic_write
    def commit(planned, writer=None):
        if getattr(planned, '_suite_replacement', False):
            return commit_replacement(engine, planned, writer)
        return original_commit(planned, engine.atomic_write if writer is None else writer)
    engine.commit = commit
    return module


def scoped_surface_hook(module, manager, name, source, url):
    """Shared controls are never blanket recolored: opt in after native host registration.
    All existing bytes survive after removing this precisely bounded import/loader.
    """
    text=source.decode('utf-8-sig')
    marker='// VOID REGISTERED NATIVE SURFACE V8'
    if marker in text:
        return source
    tokens,pairs=module.scan(manager,text)
    opening=next((i for i,token in enumerate(tokens) if token.text=='{'),None)
    expected=('Dialog','iControl') if name=='UiDialog.qml' else ('Window','rootWindow')
    manager.require(opening is not None and tokens[opening-1].text==expected[0], 'Unrecognized shared surface root: '+name)
    identities=[]
    i=opening+1
    while i<pairs[opening]:
        if tokens[i].text in ('{','[','(') and i in pairs:
            i=pairs[i]+1;continue
        if [token.text for token in tokens[i:i+2]]==['id',':']:
            identities.append(tokens[i+2].text)
        i+=1
    manager.require(identities==[expected[1]], 'Unrecognized shared surface identity: '+name)
    window=expected[1]+'.contentItem.Window.window' if name=='UiDialog.qml' else expected[1]
    block='\n'+marker+'\nLoader { objectName:"voidRegisteredNativeSurface"; visible:false; active:VoidSurfaceSkin.SurfaceRegistry.owns('+window+'); source:'+json.dumps(url+'NativeControlStyle.qml')+'; onLoaded:{ item.stylePanels=true; item.pages='+expected[1]+' } }\n'
    start=tokens[opening].end
    added='import '+json.dumps(url)+' as VoidSurfaceSkin\n'
    # The attached Window lookup is only used on the Popup contentItem (an Item).
    window_import='' if re.search(r'^import QtQuick.Window ',text,re.M) else 'import QtQuick.Window 2.15\n'
    result=text[:start]+block+text[start:]
    at=tokens[opening-1].start
    result=result[:at]+added+window_import+result[at:]
    manager.require(result.replace(added+window_import,'',1).replace(block,'',1)==text, 'Shared surface callback preservation failed.')
    module.scan(manager,result)
    return (('\ufeff' if source.startswith(b'\xef\xbb\xbf') else '')+result).encode('utf-8')


def writable_windows_target(path):
    """Temporarily clear only the DOS read-only bit, never ACLs/security settings."""
    import os
    import stat
    if os.name == 'nt' and path.exists():
        mode = path.stat().st_mode
        if not mode & stat.S_IWRITE:
            os.chmod(path, mode | stat.S_IWRITE)
            return mode
    return None


def remove_ui_file(path):
    import os
    previous_mode = writable_windows_target(path)
    try:
        path.unlink()
    except BaseException:
        if previous_mode is not None and path.exists():
            os.chmod(path, previous_mode)
        raise


def atomic_write(path, body, mode=None):
    """Windows-safe staging, including read-only EXE/assets and failed cleanup.
    The historical writer chmod'ed the stage before replacing the target. On
    Windows a read-only target blocks replace and a read-only stage blocks unlink,
    masking the original error with WinError 5 on .void-stage-*.
    """
    import os
    import tempfile
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary_name = tempfile.mkstemp(prefix='.void-stage-', dir=path.parent)
    temporary = Path(temporary_name)
    previous_mode = None
    replaced = False
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(body); stream.flush(); os.fsync(stream.fileno())
        if os.name != 'nt' and mode is not None:
            os.chmod(temporary, mode)
        previous_mode = writable_windows_target(path)
        os.replace(temporary, path)
        replaced = True
        if os.name == 'nt' and mode is not None:
            os.chmod(path, mode)  # reapply read-only only AFTER successful replacement
        if os.name != 'nt':
            directory = os.open(path.parent, os.O_RDONLY | getattr(os, 'O_DIRECTORY', 0))
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
    finally:
        active_error = sys.exc_info()[0] is not None
        try:
            if not replaced and previous_mode is not None and path.exists():
                os.chmod(path, previous_mode)
        finally:
            if temporary.exists():
                try:
                    remove_ui_file(temporary)
                except OSError:
                    if not active_error:
                        raise  # never hide the original write/replace failure


def replacement_state(module, root, current, owners, url):
    """Explicit replacement: derive identities from actual files, not stale state.
    Fresh native core callbacks still go through the reviewed-source transformer.
    Recognized already-installed bridge resources are retained and sanitized.
    """
    engine = module.manager().engine()
    resources = {}
    for name, value in current.items():
        if name in module.RESOURCE_NAMES and (b'VOID REFERENCE UI V3' in value or b'VOID NATIVE SETTINGS V4' in value):
            resources[name] = {'sha256': sha(value), 'container': owners[name].relative_to(root).as_posix()}
    assets = {}
    for name in module.payload():
        if name.startswith(PREFIX):
            target = engine.safe_target(root, name)
            engine.need(not target.exists() or (target.is_file() and target.stat().st_size <= 2*1024*1024), 'Oversized/non-file UI asset: '+name)
            assets[name] = engine.digest(target.read_bytes() if target.exists() else None)
    return {'format': 1, 'edition': 5, 'assets_url': url, 'resources': resources, 'assets': assets}


def retire_old_assets(module, planned, packed):
    """Remove obsolete files ONLY within this pack's UI directory, with undo bytes."""
    engine = module.manager().engine()
    directory = engine.safe_target(planned.root, PREFIX.rstrip('/'))
    count = total = 0
    if directory.exists():
        engine.need(directory.is_dir(), 'UI asset path must be a directory.')
        for path in sorted(directory.rglob('*')):
            relative = path.relative_to(planned.root).as_posix()
            engine.safe_target(planned.root, relative)  # rejects links/junctions/escapes
            if path.is_dir():
                continue
            count += 1
            engine.need(path.is_file() and path.stat().st_size <= 2*1024*1024 and count <= 512, 'Oversized/unsupported old UI folder.')
            total += path.stat().st_size
            engine.need(total <= 32*1024*1024, 'Old UI asset folder exceeds replacement limit.')
            if relative not in packed:
                planned.changes.append(engine.Change(relative, path.read_bytes(), None))
    engine.need(len(planned.changes) <= 256, 'Too many replacement files for a single undo journal.')
    planned._suite_replacement = True


def commit_replacement(engine, planned, writer=None):
    """Pinned transaction semantics extended to journalled deletion of old UI files.
    The pinned restore already supports after=None; no file is deleted pre-backup.
    """
    import os
    import stat
    from datetime import datetime, timezone
    import uuid
    writer = engine.atomic_write if writer is None else writer
    if not planned.changes:
        return None
    engine.need(len(planned.changes) <= 256 and len({change.relative for change in planned.changes}) == len(planned.changes), 'Invalid replacement record count/duplicates.')
    for change in planned.changes:
        engine.need(engine.current(engine.safe_target(planned.root, change.relative)) == change.before, 'Target changed after preflight: '+change.relative)
    backup_root = engine.safe_target(planned.root, 'VOID-UI-Backups')
    backup_root.mkdir(exist_ok=True)
    folder = backup_root/(datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-')+uuid.uuid4().hex[:8])
    folder.mkdir(mode=0o700)
    state = {'format': 1, 'installation': str(planned.root), 'status': 'preparing', 'records': []}
    for index, change in enumerate(planned.changes):
        target = engine.safe_target(planned.root, change.relative)
        info = target.stat() if target.exists() else None
        row = {'path': change.relative, 'before': engine.digest(change.before), 'after': engine.digest(change.after),
               'backup': 'files/%03d.bin' % index, 'mode': stat.S_IMODE(info.st_mode) if info else None,
               'atime_ns': info.st_atime_ns if info else None, 'mtime_ns': info.st_mtime_ns if info else None}
        if change.before is not None:
            engine.atomic_write(folder/row['backup'], change.before, 0o600)
            engine.need((folder/row['backup']).read_bytes() == change.before, 'Backup read-back failed.')
        state['records'].append(row)
    state['status'] = 'ready'; engine.save_state(folder, state)
    try:
        state['status'] = 'applying'; engine.save_state(folder, state)
        for row, change in zip(state['records'], planned.changes):
            target = engine.safe_target(planned.root, change.relative)
            engine.need(engine.current(target) == change.before, 'Target changed during replacement: '+change.relative)
            if change.after is None:
                remove_ui_file(target)
            else:
                writer(target, change.after, row['mode'])
            engine.need(engine.current(target) == change.after, 'Replacement read-back failed: '+change.relative)
        state['status'] = 'applied'; engine.save_state(folder, state)
    except BaseException as error:
        try:
            engine.restore(folder, planned.root, rollback=True)
        except BaseException as recovery:
            raise engine.Refusal('Replacement interrupted; preserve backup: '+str(folder)+'\n'+str(recovery)) from error
        hint = '\nWindows denied the write. Close all player processes and run REPLACE-INSTALLED-UI.cmd as administrator. If access is still denied, check the folder/file permissions or file lock; this installer does not change ACLs or security software.' if isinstance(error, PermissionError) else ''
        raise engine.Refusal('Replacement failed; original files restored. Backup: '+str(folder)+'\n'+str(error)+hint) from error
    return folder


def without_optimizer(module, desired, url):
    """Remove only exact engine-owned additions; never rewrite native callbacks."""
    result = dict(desired)
    for name in ('UiSettingsWindow.qml', 'PlrSidebar.qml'):
        original = result[name]
        text = original.decode('utf-8-sig')
        if name == 'UiSettingsWindow.qml':
            text = text.replace(module.ROOT_BLOCK, '\n// VOID REFERENCE UI V3 NATIVE-ONLY\nfunction fVoidSelectNative(index) { fChangeSelectedSetting(index) }\n')
            text = text.replace(module.SWITCH_BLOCK, '').replace(module.COMPLETE_BLOCK, '')
        else:
            block = module.SIDEBAR_BLOCK.replace('SIDEBAR_SKIN_URL', json.dumps(url+'SidebarSkin.qml'))
            loader = '\n// VOID REFERENCE UI V3 NATIVE-ONLY\nLoader { objectName:"voidReferenceSidebarSkinLoader"; source:'+json.dumps(url+'SidebarSkin.qml')+'; onLoaded:item.host=iSidebar }\n'
            text = text.replace(block, loader).replace(module.SIDEBAR_ITEM, '')
        text = text.replace('import '+json.dumps(url+'Navigation.js')+' as VoidUiNav\n', '')
        if any(token in text for token in ('fVoidSelectOptimizer', 'voidOptimizer', 'VoidUiNav', 'pVoidOptimizer')):
            raise RuntimeError('Unrecognized optimizer integration; no partial removal: '+name)
        module.scan(module.manager(), text)
        result[name] = (('\ufeff' if original.startswith(b'\xef\xbb\xbf') else '')+text).encode('utf-8')
    return result


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


def validate_presets(presets):
    if not isinstance(presets,list) or len(presets)!=6:
        raise RuntimeError('Exactly six reviewed dark presets are required.')
    ids=set()
    for entry in presets:
        if set(entry)!={'id','name','description','radius','durationMs','palette'} or not re.fullmatch(r'[a-z]{2,16}',entry['id']) or entry['id'] in ids:
            raise RuntimeError('Invalid/duplicate preset definition.')
        ids.add(entry['id'])
        configure({'format':1,'name':entry['name'],'font':'Segoe UI','radius':entry['radius'],'motion':True,'durationMs':entry['durationMs'],'palette':entry['palette']},{'palette':{},'corners':{},'animations':{}})
        if not isinstance(entry['description'],str) or len(entry['description'])>100:
            raise RuntimeError('Invalid preset description.')
    if presets[0]['id']!='oled':raise RuntimeError('OLED must be the default preset.')


def theme_source(design, config, template=None, presets=None):
    template=template if template is not None else (ROOT/'assets/full-emulator-ui/ThemeRuntime.qml').read_bytes()
    presets=json.loads(json.dumps(presets if presets is not None else json.loads((ROOT/'assets/full-emulator-ui/themes.json').read_bytes())))
    validate_presets(presets)
    presets[0].update(palette=design['palette'],radius=design['radius'],durationMs=design['durationMs'])
    return template.decode('utf-8').replace('PRESET_DEFINITIONS',json.dumps(presets,ensure_ascii=True)).replace('DEFAULT_DESIGN',json.dumps(design,ensure_ascii=True)).encode('utf-8')


def live_theme_sources(module, desired, url):
    """Convert only lexer-recognized visual literal slots to live Theme bindings.
    Native functions, persistence, strings/actions/values and protected sources
    are outside these slots; no source-wide color/string replacement is used.
    """
    manager=module.manager();config=module.design_config
    aliases={}
    # Earlier shipped native palettes are normalized on upgrade too.
    historical=[{'background':'#080809','surface':'#111116','raised':'#23232d','border':'#353542','text':'#eeeef4','muted':'#a0a0b0'},config['palette']]
    for palette in historical:
        for role,value in palette.items():aliases.setdefault(value.lower(),role)
    for role,value in config['palette'].items():aliases.setdefault(value.lower(),role)
    for role in ('background','surface','raised','border','text','muted','accent'):
        aliases[config['palette'][role].lower()]=role
    result=dict(desired);coverage=[]
    for name,body in desired.items():
        if name not in module.live_names or not name.endswith('.qml'):continue
        source=body.decode('utf-8-sig')
        slots,unsupported,_,_=manager.visual_slots(source)
        edits=[]
        for start,end,kind,value,line,binding in slots:
            role=None
            if kind=='font':replacement='VoidLiveTheme.Theme.fontFamily'
            else:
                if kind=='qt':role={key.casefold():role for key,role in config['qt_colors'].items()}.get(value.casefold())
                else:role=aliases.get(value.lower())
                if role is None:continue
                replacement='VoidLiveTheme.Theme.'+role
            if source[start:end]!=replacement:edits.append((start,end,replacement))
        # Dynamic font descriptors created by the native page-density transform.
        # Restrict to the visual 'font' binding and its literal family member;
        # do not walk arbitrary JS object data or native preference values.
        tokens,scopes=manager.scopes_and_bindings(source)
        occupied={(a,b) for a,b,_ in edits}
        for scope in scopes.values():
            span=scope['bindings'].get('font')
            if not span:continue
            a,b=span
            if ''.join(token.text for token in tokens[a:min(a+4,b)])!='Qt.font(':continue
            for i in range(a+4,b-2):
                token=tokens[i+2]
                if tokens[i].text=='family' and tokens[i+1].text==':' and token.kind=='string' and token.text[1:-1]==config['font_family'] and (token.start,token.end) not in occupied:
                    edits.append((token.start,token.end,'VoidLiveTheme.Theme.fontFamily'))
        _,decor_scopes,decorations=manager.decorative_slots(source)
        for start,end,kind,value,line,opening in decorations:
            if kind=='radius' and float(value)>0 and float(value)==config['corners']['radius'] and manager.matches(name,config['corners']['files']):
                bindings=decor_scopes[opening]['bindings'];dimensions=[]
                for dimension in ('width','height'):
                    span=bindings.get(dimension)
                    if span and span[1]==span[0]+1 and re.fullmatch(r'[0-9]+(?:\.[0-9]+)?',tokens[span[0]].text):dimensions.append(float(tokens[span[0]].text))
                if len(dimensions)==2 and dimensions[0]==dimensions[1] and float(value)>=dimensions[0]/2:continue
                edits.append((start,end,'VoidLiveTheme.Theme.corner'))
            elif kind=='duration' and int(float(value))==config['animations']['color_ms'] and manager.matches(name,config['animations']['files']):
                edits.append((start,end,'VoidLiveTheme.Theme.duration'))
        if edits:
            source=manager.substitute(source,edits)
            imported='import '+json.dumps(url)+' as VoidLiveTheme\n'
            if imported not in source:
                imports=list(re.finditer(r'^import[^\r\n]*[\r\n]+',source,re.M))
                manager.require(bool(imports),'Missing native imports: '+name)
                at=imports[-1].end();source=source[:at]+imported+source[at:]
            module.scan(manager,source)
            result[name]=(('\ufeff' if body.startswith(b'\xef\xbb\xbf') else '')+source).encode('utf-8')
        coverage.append({'name':name,'new_bindings':len(edits),'live': 'VoidLiveTheme.Theme.' in source,'unmapped_visual_bindings':unsupported})
    module.live_coverage=coverage
    return result


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
    module.replacing_ui = '--replace-ui' in argv
    if module.replacing_ui:
        print('REPLACE UI: old UI files/state will be replaced; apps/data and independent helpers are not removed.')
    print('VOID OLED STUDIO 09 - live native appearance / six presets / custom colors')
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
