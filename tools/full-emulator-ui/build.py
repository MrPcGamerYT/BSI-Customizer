"""Build a self-contained source-runtime suite; no player binary/download at runtime."""
import base64
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import zipfile
import zlib

ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent


def archive(members):
    stream=io.BytesIO()
    with zipfile.ZipFile(stream,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as zipped:
        for name,data in sorted(members.items()):
            item=zipfile.ZipInfo(name,(2026,10,25,0,0,0));item.compress_type=zipfile.ZIP_DEFLATED
            item.external_attr=0o100644<<16;zipped.writestr(item,data)
    return stream.getvalue()


def main():
    spec=importlib.util.spec_from_file_location('suite_builder',HERE/'suite.py')
    suite=importlib.util.module_from_spec(spec);spec.loader.exec_module(suite)
    runtime=suite.runtime()
    assets={name[len(suite.PREFIX):]:data for name,data in runtime.payload().items() if name.startswith(suite.PREFIX)}
    assets_blob=archive(assets)
    legacy=(ROOT/'reference/full-emulator-ui/legacy_installer.py').read_bytes()
    source=(HERE/'suite.py').read_text(encoding='utf-8')
    source=source.replace("LEGACY_B85 = b''  # bundled by build.py",'LEGACY_B85 = '+repr(base64.b85encode(zlib.compress(legacy,9))))
    source=source.replace("ASSETS_B85 = b''  # bundled by build.py",'ASSETS_B85 = '+repr(base64.b85encode(assets_blob)))
    source=source.replace("LEGACY_SHA256 = ''",'LEGACY_SHA256 = '+repr(suite.sha(legacy)))
    source=source.replace("ASSETS_SHA256 = ''",'ASSETS_SHA256 = '+repr(suite.sha(assets_blob)))
    compile(source,'VOID-Full-UI.py','exec')
    members={'VOID-Full-UI.py':source.encode('utf-8'),'README.md':(HERE/'README.md').read_bytes(),
             'design.json':assets['design.json'],'LICENSE':(ROOT/'LICENSE').read_bytes(),
             'Source/suite.py':(HERE/'suite.py').read_bytes(),'Source/build.py':Path(__file__).read_bytes(),
             'Source/legacy_installer.py':legacy,'Source/PROVENANCE.json':(ROOT/'reference/full-emulator-ui/PROVENANCE.json').read_bytes()}
    members.update({suite.PREFIX+name:data for name,data in assets.items()})
    run='''@echo off
setlocal
set "PYTHONUTF8=1"
where py >nul 2>nul
if errorlevel 1 goto python
py -3 "%~dp0VOID-Full-UI.py" %*
exit /b %errorlevel%
:python
where python >nul 2>nul
if errorlevel 1 goto missing
python "%~dp0VOID-Full-UI.py" %*
exit /b %errorlevel%
:missing
echo Python 3.9+ is required. No files were changed.
exit /b 1
'''
    def cmd(text):return text.replace('\n','\r\n').encode('utf-8')
    members['RUN-UI.cmd']=cmd(run)
    for name,mode in [('CHECK-UI','--check'),('INSTALL-UI','--apply'),('REPLACE-INSTALLED-UI','--apply --replace-ui'),('CHECK-REPLACEMENT','--check --replace-ui'),('UNDO-UI','--restore-latest')]:
        members[name+'.cmd']=cmd('@echo off\ncall "%~dp0RUN-UI.cmd" '+mode+' --report "%~dp0UI-COVERAGE.json" %*\nset "RC=%ERRORLEVEL%"\npause\nexit /b %RC%\n')
    members['DIAGNOSE-UI.cmd']=cmd('@echo off\ncall "%~dp0RUN-UI.cmd" --diagnose-ui --diagnostic-output "%~dp0VOID-UI-Diagnostics.zip" %*\nset "RC=%ERRORLEVEL%"\npause\nexit /b %RC%\n')
    members['EXPORT-ALL-UI-SOURCES.cmd']=cmd('@echo off\ncall "%~dp0RUN-UI.cmd" --export-sources "%~dp0PLANNED-ALL-UI-SOURCES.zip" %*\nset "RC=%ERRORLEVEL%"\npause\nexit /b %RC%\n')
    screenshot=ROOT/'.cache/full-ui/suite07-fixture.png'
    if screenshot.is_file():members['FIXTURE-PREVIEW-NOT-OEM.png']=screenshot.read_bytes()
    report={'suite':'VOID Carbon Suite 07','sourceCommit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
            'legacyEngineSha256':suite.sha(legacy),'suiteSourceSha256':suite.sha((HERE/'suite.py').read_bytes()),
            'assetsSha256':{name:suite.sha(data) for name,data in assets.items()},
            'optimizerIncluded':False,'helperIncluded':False,'replacementInstallSupported':True,
            'proprietaryBinariesIncluded':False,'liveEmulatorExecuted':False,
            'scope':'All mapped eligible native QML/SVG files; boot/auth/JS/framework/dependencies protected; per-install coverage required.'}
    members['BUILD-REPORT.json']=(json.dumps(report,indent=2)+'\n').encode()
    members['MANIFEST-SHA256.txt']=''.join(suite.sha(data)+'  '+name+'\n' for name,data in sorted(members.items())).encode()
    destination=ROOT/'deliverables/VOID-Full-Emulator-UI-Suite07.zip'
    destination.parent.mkdir(exist_ok=True)
    destination.write_bytes(archive({'VOID-Full-Emulator-UI-Suite07/'+name:data for name,data in members.items()}))
    destination.with_suffix('.zip.sha256').write_text(suite.sha(destination.read_bytes())+'  '+destination.name+'\n')
    print(destination,len(destination.read_bytes()))


if __name__=='__main__':main()
