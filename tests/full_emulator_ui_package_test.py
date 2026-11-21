"""Release smoke test reproduces D:\\<extracted-pack> on Windows/Python 3.13."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
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
print('PASS: ZIP hashes, no optimizer/helper, shallow drive-root startup and external profile on', sys.version)
