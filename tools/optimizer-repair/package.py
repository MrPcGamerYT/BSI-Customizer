"""Package only the matched optimizer page/helper; no shared theme/icon/boot files."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import zipfile
ROOT=Path(__file__).resolve().parents[2]
base=ROOT/'assets/optimizer-repair'
exe=Path(os.environ['VOID_OPTIMIZER_TESTED_EXE'])
if not exe.is_file() or exe.read_bytes()[:2]!=b'MZ':raise SystemExit('Tested Windows helper EXE required.')
stage=ROOT/'.cache/optimizer-repair/package/VOID-Optimizer-Studio04'
if stage.exists(): shutil.rmtree(stage)
stage.mkdir(parents=True,exist_ok=True)
shutil.copytree(base,stage,dirs_exist_ok=True)
shutil.copytree(ROOT/'tools/optimizer-repair/Source',stage/'Source',dirs_exist_ok=True)
shutil.copyfile(exe,stage/'VOID-Optimizer.exe')
shutil.copyfile(ROOT/'LICENSE',stage/'LICENSE')
source=ROOT/'tools/optimizer-repair/Source/Optimizer.cs'
report={'sourceCommit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        'sourceSha256':hashlib.sha256(source.read_bytes()).hexdigest(),
        'helperSha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
        'uiVersion':'STUDIO 04', 'protocol':'voidoptimizer-v3', 'targetedWindowsInstallerTests':True, 'windowsSelfTests':True,'windowsFakePlatformUiSmoke':True,
        'protocolRegisteredDuringBuild':False, 'realTuningAppliedDuringBuild':False,
        'provenance':'Matched helper/page adapted from BSI-Customizer bc4c68522878f7c060e82a3524e127099d4b0c54. OEM emulator not executed.'}
(stage/'BUILD-REPORT.json').write_text(json.dumps(report,indent=2)+'\n')
destination=ROOT/'deliverables/VOID-Optimizer-Studio04.zip'
destination.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(destination,'w',zipfile.ZIP_DEFLATED) as z:
    for p in sorted(stage.rglob('*')):
        if p.is_file():z.write(p,'VOID-Optimizer-Studio04/'+p.relative_to(stage).as_posix())
(destination.with_suffix('.zip.sha256')).write_text(hashlib.sha256(destination.read_bytes()).hexdigest()+'  '+destination.name+'\n')
print(destination)
