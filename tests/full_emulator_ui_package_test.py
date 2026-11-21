"""Release smoke test reproduces D:\\<extracted-pack> on Windows/Python 3.13."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import stat
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
with zipfile.ZipFile(ROOT/'deliverables/VOID-Full-Emulator-UI-Suite07.zip') as archive:
    prefix = 'VOID-Full-Emulator-UI-Suite07/'
    assert archive.testzip() is None
    assert not any('Helper/' in name or name.lower().endswith('.exe') or name.endswith('OptimizerPage.qml') for name in archive.namelist())
    report = json.loads(archive.read(prefix+'BUILD-REPORT.json'))
    assert report['optimizerIncluded'] is False and report['helperIncluded'] is False
    assert report['replacementInstallSupported'] is True
    assert '--apply --replace-ui' in archive.read(prefix+'REPLACE-INSTALLED-UI.cmd').decode()
    for line in archive.read(prefix+'MANIFEST-SHA256.txt').decode().splitlines():
        digest, name = line.split('  ', 1)
        assert hashlib.sha256(archive.read(prefix+name)).hexdigest() == digest, name
    # Exactly one folder below a drive root: parents[2] must never be evaluated.
    directory = Path.cwd().anchor if os.name == 'nt' else None
    with tempfile.TemporaryDirectory(prefix='VOID-UI-pack-', dir=directory) as folder:
        target = Path(folder)
        for name in ('VOID-Full-UI.py', 'design.json'):
            (target/name).write_bytes(archive.read(prefix+name))
        if os.name == 'nt':
            assert len((target/'VOID-Full-UI.py').parents) == 2
        result = subprocess.run([sys.executable, str(target/'VOID-Full-UI.py'), '--help'], cwd=target, text=True, capture_output=True, timeout=60)
        assert result.returncode == 0, result.stdout+result.stderr
        assert 'Traceback' not in result.stderr
        assert '--replace-ui' in result.stdout
        # Exercise the installed native-only payload, not just the argument parser.
        probe = '''import importlib.util
from pathlib import Path
s=importlib.util.spec_from_file_location('pack',Path('VOID-Full-UI.py'))
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
u=m.runtime();p=u.payload()
assert not any('OptimizerPage.qml' in name or 'VoidOptimizerUI/' in name for name in p if name.startswith(m.PREFIX))
assert b'fVoidSelectOptimizer' not in p[m.PREFIX+'SettingsShell.qml']
assert b'voidSettingsOptimizerLoader' not in p[m.PREFIX+'SettingsShell.qml']
assert b'REGRESSION FONT' in p[m.PREFIX+'Theme.qml']
print('PASS extracted runtime and external design')
'''
        profile = json.loads((target/'design.json').read_text())
        profile['font'] = 'REGRESSION FONT'
        (target/'design.json').write_text(json.dumps(profile))
        result = subprocess.run([sys.executable, '-c', probe], cwd=target, text=True, capture_output=True, timeout=60)
        assert result.returncode == 0, result.stdout+result.stderr
        import runpy
        helpers = runpy.run_path(str(ROOT/'tests/full_emulator_ui_suite_test.py'))
        player = target/'Synthetic Player'
        original, _ = helpers['fixture'](player)
        (player/'.VOID-Reference-UI.json').write_bytes(b'stale broken old record')
        old = player/'Assets/VOIDReferenceUI/obsolete-optimizer.qml'
        old.parent.mkdir(parents=True);old.write_bytes(b'old UI')
        if os.name == 'nt':
            (player/'HD-Player.exe').chmod(stat.S_IREAD)
            old.chmod(stat.S_IREAD)
        result = subprocess.run([sys.executable, str(target/'VOID-Full-UI.py'), '--apply', '--replace-ui', '--yes', '--install-dir', str(player)], cwd=target, text=True, capture_output=True, timeout=120)
        assert result.returncode == 0, result.stdout+result.stderr
        assert not old.exists()
        assert (player/'HD-Player.exe').read_bytes() != original
        assert b'CARBON SUITE 07' in (player/'Assets/VOIDReferenceUI/SettingsShell.qml').read_bytes()
        result = subprocess.run([sys.executable, str(target/'VOID-Full-UI.py'), '--restore-latest', '--yes', '--install-dir', str(player)], cwd=target, text=True, capture_output=True, timeout=120)
        assert result.returncode == 0, result.stdout+result.stderr
        assert (player/'HD-Player.exe').read_bytes() == original
        assert old.read_bytes() == b'old UI'
        if os.name == 'nt':
            assert not (player/'HD-Player.exe').stat().st_mode & stat.S_IWRITE
            assert not old.stat().st_mode & stat.S_IWRITE
        assert (player/'.VOID-Reference-UI.json').read_bytes() == b'stale broken old record'
print('PASS: ZIP hashes, no optimizer/helper, shallow drive-root startup and external profile on', sys.version)
