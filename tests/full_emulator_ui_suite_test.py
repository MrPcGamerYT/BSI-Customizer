"""Mapped source/PE transaction tests; no proprietary binary is executed."""
import copy
import importlib.util
import json
import os
import stat
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('suite07',ROOT/'tools/full-emulator-ui/suite.py')
S=importlib.util.module_from_spec(spec);spec.loader.exec_module(S)
U=S.runtime();M=U.manager();E=M.engine()
sys.path.insert(0,str(ROOT/'tests'))
import full_ui_pe_fixture as F
F.A=E


def fixture(root):
    root.mkdir()
    payload=U.payload()
    sources={p.name:p.read_bytes() for p in (ROOT/'reference/full-emulator-ui/native').glob('*') if p.suffix in ('.qml','.js')}
    sources.update({name:payload['baseline/pages/'+name] for name in U.PAGE_NAMES})
    sources['UiSettingsWindow.qml']=payload['baseline/UiSettingsWindow.qml']
    sources['PlrSidebar.qml']=payload['baseline/PlrSidebar.qml']
    sources['BootSplash.qml']=b'Item { function authBoot(){backend.authenticate()} Rectangle {color:"red"} }'
    sources['Login.qml']=b'Item { property string account:""; function signIn(){backend.login()} }'
    sources['VolumePopup.qml']=b'import QtQuick 2.15\nItem { property real volume: 0.5; Rectangle { color: UiTheme.colors.primary80; border.width:1 } function apply(){ backend.setVolume(volume) } }'
    sources['ExitDialog.qml']=b'import QtQuick 2.15\nItem { property bool accepted:false; Rectangle { color: UiTheme.colors.primary80; border.width:1 } function confirm(){backend.exitPlayer()} }'
    entries={'Player/'+name:(value,1) for name,value in sources.items()}
    body=F.pe(F.rcc(entries),registration=True,extra=b''.join(F.unit(name) for name in sources))
    (root/'HD-Player.exe').write_bytes(body)
    return body,sources


def inventory(body):
    return {row.name:E.unpack_record(body,row.entry,row.flags) for row in E.resources_in(body,{},{})[1]}


def files(root):
    return {p.relative_to(root).as_posix():S.sha(p.read_bytes()) for p in root.rglob('*') if p.is_file()}


class SuiteTests(unittest.TestCase):
    def test_full_inventory_apply_idempotence_and_exact_restore(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)/'Player with spaces';original,before=fixture(root)
            planned,report=U.plan_install(root)
            names={row['name'] for row in report['coverage']}
            self.assertTrue({'VolumePopup.qml','ExitDialog.qml','KmmEditorWindow.qml','PlrTopbar.qml','UiSettingsWindow.qml','UiPerformanceSetting.qml'}<=names)
            self.assertFalse(report['all_ui_runtime_certified'])
            self.assertEqual(report['theme_presets'],['OLED Black','Midnight Blue','Amethyst','Forest','Ember','Rose Noir'])
            self.assertFalse((root/U.UI_STATE).exists(),'preflight is read-only')
            backup=E.commit(planned)
            after=inventory((root/'HD-Player.exe').read_bytes())
            for name in ('BootSplash.qml','Login.qml','KmmUtils.js','plr.qml'):
                self.assertEqual(after[name],before[name],name)
            for name in ('VolumePopup.qml','ExitDialog.qml'):
                self.assertIn(b'VOID SUITE07 PASSIVE CONTROLS',after[name])
                self.assertIn(b'backend.',after[name])
                self.assertIn(b'VoidLiveTheme.Theme.',after[name])
            self.assertIn(b'VoidLiveTheme.Theme.',after['UiPerformanceSetting.qml'])
            self.assertNotIn(b'VoidLiveTheme.Theme.',after['UiDialog.qml'])
            self.assertIn(b'OLED STUDIO 09',(root/'Assets/VOIDReferenceUI/SettingsShell.qml').read_bytes())
            self.assertFalse((root/'Assets/VOIDReferenceUI/OptimizerPage.qml').exists())
            for name in ('UiSettingsWindow.qml', 'PlrSidebar.qml'):
                for token in (b'voidOptimizer', b'pVoidOptimizer', b'VoidUiNav', b'F9'):
                    self.assertNotIn(token, after[name])
            self.assertEqual((root/'Assets/VOIDReferenceUI/Icons.js').read_bytes(),(ROOT/'assets/settings-icons-repair/Shared-Recovery/Icons.js').read_bytes())
            self.assertEqual([c.relative for c in U.plan_install(root)[0].changes],[],'repeat must verify, not stack loaders/animations')
            E.restore(backup,root)
            self.assertEqual((root/'HD-Player.exe').read_bytes(),original)
            self.assertFalse((root/U.UI_STATE).exists())

    def test_upgrade_tracked_v5_with_current_optimizer_and_exact_undo(self):
        source=(ROOT/'reference/full-emulator-ui/legacy_installer.py').read_bytes()
        import types
        old=types.ModuleType('old_suite_fixture');old.__file__=str(ROOT/'tools/full-emulator-ui/suite.py')
        sys.modules[old.__name__]=old;exec(compile(source,'old-fixture.py','exec'),old.__dict__)
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)/'Upgraded Player';fixture(root)
            E.commit(old.plan_install(root,full_emulator_ui=True)[0])
            # Simulate the state-aware Studio04 repair, not an untracked overwrite.
            state_file=root/U.UI_STATE;state=json.loads(state_file.read_bytes())
            source_root=ROOT/'assets/optimizer-repair/Assets'
            for path in source_root.rglob('*'):
                if not path.is_file():continue
                relative=S.PREFIX+path.relative_to(source_root).as_posix()
                target=root/relative;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(path.read_bytes())
                state['assets'][relative]=S.sha(path.read_bytes())
            state_file.write_text(json.dumps(state))
            before=files(root)
            planned,report=U.plan_install(root)
            backup=E.commit(planned)
            self.assertNotIn(b'voidOptimizer', inventory((root/'HD-Player.exe').read_bytes())['PlrSidebar.qml'])
            self.assertEqual([c.relative for c in U.plan_install(root)[0].changes],[])
            E.restore(backup,root)
            # Recovery journals themselves remain, all pre-upgrade files return.
            for name,digest in before.items():
                self.assertEqual(S.sha((root/name).read_bytes()),digest,name)

    def test_asset_mismatch_is_not_adopted(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)/'Player';fixture(root);E.commit(U.plan_install(root)[0])
            target=root/'Assets/VOIDReferenceUI/SettingsShell.qml';target.write_bytes(target.read_bytes()+b'\n// external edit\n')
            before=files(root)
            with self.assertRaisesRegex(RuntimeError,'asset changed externally'):
                U.plan_install(root)
            self.assertEqual(files(root),before)

    def test_export_all_sources_is_read_only(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)/'Player';fixture(root);before=files(root)
            destination=Path(folder)/'source-preview.zip'
            S.export_sources(U,root,destination)
            self.assertEqual(files(root),before)
            with zipfile.ZipFile(destination) as archive:
                names=archive.namelist()
                self.assertTrue(any(n.endswith('/KmmEditorWindow.qml') for n in names))
                self.assertTrue(any(n.endswith('/VolumePopup.qml') for n in names))
                self.assertFalse(any(n.lower().endswith(('.exe','.dll')) for n in names))
                self.assertIn('COVERAGE.json',names)
            with self.assertRaisesRegex(RuntimeError,'outside the emulator'):
                S.export_sources(U,root,root/'preview.zip')

    def test_replace_stale_state_and_missing_assets_then_exact_undo(self):
        import shutil
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)/'Reset Player';original,_=fixture(root)
            E.commit(U.plan_install(root)[0])
            # Reproduce the report: original resources, old installed state, absent assets.
            (root/'HD-Player.exe').write_bytes(original)
            shutil.rmtree(root/'Assets/VOIDReferenceUI')
            (root/'Apps').mkdir();(root/'Apps/user-data.bin').write_bytes(b'KEEP GAME DATA')
            before=files(root)
            with self.assertRaises(Exception):U.plan_install(root)
            planned,report=U.plan_install(root,replace_existing_ui=True)
            self.assertTrue(report['replacement_install'])
            self.assertEqual(files(root),before,'replacement preflight must be read-only')
            backup=E.commit(planned)
            self.assertEqual(U.plan_install(root)[0].changes,[])
            self.assertEqual((root/'Apps/user-data.bin').read_bytes(),b'KEEP GAME DATA')
            self.assertNotIn(b'voidOptimizer',inventory((root/'HD-Player.exe').read_bytes())['PlrSidebar.qml'])
            E.restore(backup,root)
            for name,digest in before.items():self.assertEqual(S.sha((root/name).read_bytes()),digest,name)
            self.assertFalse((root/'Assets/VOIDReferenceUI/SettingsShell.qml').exists())

    def test_replace_edited_assets_deletes_obsolete_files_and_undo_restores_them(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)/'Edited UI';fixture(root)
            E.commit(U.plan_install(root)[0])
            old=root/'Assets/VOIDReferenceUI/old-optimizer.qml';old.write_bytes(b'OLD UI')
            shell=root/'Assets/VOIDReferenceUI/SettingsShell.qml';shell.write_bytes(b'OLD EDITED SHELL')
            before=files(root)
            with self.assertRaises(Exception):U.plan_install(root)
            planned,_=U.plan_install(root,replace_existing_ui=True)
            self.assertTrue(any(change.relative.endswith('old-optimizer.qml') and change.after is None for change in planned.changes))
            backup=E.commit(planned)
            self.assertFalse(old.exists())
            self.assertIn(b'OLED STUDIO 09',shell.read_bytes())
            self.assertEqual(U.plan_install(root)[0].changes,[])
            E.restore(backup,root)
            for name,digest in before.items():self.assertEqual(S.sha((root/name).read_bytes()),digest,name)

    def test_replace_malformed_state_and_failure_rolls_back_deletions(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)/'Broken state';fixture(root)
            (root/U.UI_STATE).write_bytes(b'not json')
            assets=root/'Assets/VOIDReferenceUI';assets.mkdir(parents=True)
            old=assets/'obsolete.qml';old.write_bytes(b'OLD')
            before=files(root)
            planned,_=U.plan_install(root,replace_existing_ui=True)
            # Force a failure AFTER the obsolete file is deleted, inside the journal.
            planned.changes.append(E.Change('fail-last.txt',None,b'X'))
            def fail_last(path,data,mode):
                if path.name=='fail-last.txt':raise OSError('simulated disk failure')
                E.atomic_write(path,data,mode)
            with self.assertRaisesRegex(Exception,'original files restored'):
                E.commit(planned,writer=fail_last)
            for name,digest in before.items():self.assertEqual(S.sha((root/name).read_bytes()),digest,name)
            self.assertFalse((root/'fail-last.txt').exists())
            self.assertFalse((assets/'SettingsShell.qml').exists())

    @unittest.skipUnless(os.name=='nt','Windows DOS read-only regression')
    def test_windows_readonly_staging_cleanup_and_original_error(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as folder:
            target=Path(folder)/'readonly.exe';target.write_bytes(b'old')
            target.chmod(stat.S_IREAD)
            original_mode=stat.S_IMODE(target.stat().st_mode)
            S.atomic_write(target,b'new',original_mode)
            self.assertEqual(target.read_bytes(),b'new')
            self.assertFalse(target.stat().st_mode & stat.S_IWRITE)
            with patch('os.replace',side_effect=PermissionError(5,'SIMULATED TARGET LOCK',str(target))):
                with self.assertRaisesRegex(PermissionError,'SIMULATED TARGET LOCK'):
                    S.atomic_write(target,b'blocked',original_mode)
            self.assertEqual(target.read_bytes(),b'new')
            self.assertFalse(target.stat().st_mode & stat.S_IWRITE)
            self.assertEqual(list(Path(folder).glob('.void-stage-*')),[])
            S.remove_ui_file(target)
            self.assertFalse(target.exists())

    @unittest.skipUnless(os.name=='nt','Windows DOS read-only replacement')
    def test_windows_readonly_player_assets_delete_and_exact_undo(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)/'Read-only Player';original,_=fixture(root)
            E.commit(U.plan_install(root)[0])
            player=root/'HD-Player.exe';player.write_bytes(original)
            shell=root/'Assets/VOIDReferenceUI/SettingsShell.qml';shell.write_bytes(b'EDITED SHELL')
            old=root/'Assets/VOIDReferenceUI/obsolete.qml';old.write_bytes(b'OLD')
            for path in (player,shell,old):path.chmod(stat.S_IREAD)
            before=files(root)
            planned,_=U.plan_install(root,replace_existing_ui=True)
            backup=E.commit(planned)
            self.assertFalse(old.exists())
            self.assertFalse(player.stat().st_mode & stat.S_IWRITE)
            self.assertFalse(shell.stat().st_mode & stat.S_IWRITE)
            E.restore(backup,root)
            for name,digest in before.items():self.assertEqual(S.sha((root/name).read_bytes()),digest,name)
            for path in (player,shell,old):self.assertFalse(path.stat().st_mode & stat.S_IWRITE)
            self.assertEqual(list(root.rglob('.void-stage-*')),[])

    def test_shared_surface_hooks_preserve_source_and_are_idempotent(self):
        import re
        url='file:///C:/Native%20Player/Assets/VOIDReferenceUI/'
        for name in ('UiDialog.qml','UiWindowedPopup.qml'):
            original=(ROOT/'reference/full-emulator-ui/native'/name).read_bytes()
            after=S.scoped_surface_hook(U,M,name,original,url)
            self.assertEqual(S.scoped_surface_hook(U,M,name,after,url),after)
            text=after.decode('utf-8-sig')
            self.assertIn('active:VoidSurfaceSkin.SurfaceRegistry.owns(',text)
            text=text.replace('import '+json.dumps(url)+' as VoidSurfaceSkin\n','',1)
            if name=='UiDialog.qml':text=text.replace('import QtQuick.Window 2.15\n','',1)
            text=re.sub(r'\n// VOID REGISTERED NATIVE SURFACE V8\nLoader \{[^\n]*\}\n','',text,count=1)
            self.assertEqual(text,original.decode('utf-8-sig'),name)

    def test_live_visual_slots_preserve_data_callbacks_and_circles(self):
        source=b'''import QtQuick 2.15
Item {
 property string storedColor:"#f2f7ff"
 function save(){ backend.save("#f2f7ff",16,160) }
 Rectangle {width:100;height:40;color:"#0b0f14";radius:16}
 Rectangle {width:32;height:32;color:"#8cc8ff";radius:16}
 Text {text:"#f2f7ff";color:"#f2f7ff";font:Qt.font({family:"Segoe UI",pixelSize:14})}
}
'''
        old=U.live_names
        try:
            U.live_names={'ExitDialog.qml'}
            result=S.live_theme_sources(U,{'ExitDialog.qml':source},'file:///theme/')['ExitDialog.qml']
        finally:U.live_names=old
        self.assertIn(b'property string storedColor:"#f2f7ff"',result)
        self.assertIn(b'function save(){ backend.save("#f2f7ff",16,160) }',result)
        self.assertIn(b'width:32;height:32;color:VoidLiveTheme.Theme.accent;radius:16',result)
        self.assertIn(b'text:"#f2f7ff";color:VoidLiveTheme.Theme.text',result)
        self.assertIn(b'font:Qt.font({family:VoidLiveTheme.Theme.fontFamily,pixelSize:14})',result)
        self.assertIn(b'radius:VoidLiveTheme.Theme.corner',result)

    def test_design_is_data_not_qml(self):
        design=json.loads((ROOT/'assets/full-emulator-ui/design.json').read_text())
        for key,value in [('font','Segoe UI"; backend.erase()'),('radius',True),('durationMs',9999),('motion','true')]:
            bad=copy.deepcopy(design);bad[key]=value
            with self.assertRaises(RuntimeError):S.configure(bad,json.loads(U.payload()['theme.json']))
        bad=copy.deepcopy(design);bad['palette']['text']='Qt.binding(function(){})'
        with self.assertRaises(RuntimeError):S.configure(bad,json.loads(U.payload()['theme.json']))


if __name__=='__main__':unittest.main(verbosity=2)
