#!/usr/bin/env python3
"""Build the single-file offline HTML composer; no dev server, EXE or user source."""
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[2]
folder=ROOT/'downloads/Player-Chrome-Monochrome'
folder.mkdir(parents=True,exist_ok=True)
pako=ROOT/'.cache/player-chrome-deps/node_modules/pako/dist/pako.min.js'
if not pako.exists():raise SystemExit('Build dependency missing: npm install --prefix .cache/player-chrome-deps pako@2.1.0')
page=(ROOT/'tools/player-chrome/Composer.html').read_text()
def safe_script(text):return text.replace('</script','<\\/script').replace('</SCRIPT','<\\/SCRIPT')
page=page.replace('/*PAKO*/',safe_script(pako.read_text()))
page=page.replace('/*CORE*/',safe_script((ROOT/'tools/player-chrome/core.js').read_text()))
page=page.replace('/*FACE*/',json.dumps((ROOT/'assets/emulator-chrome/InlineFace.qml.txt').read_text()))
(folder/'PlayerChrome.html').write_text(page,encoding='utf-8')
(folder/'pako-LICENSE.txt').write_bytes((pako.parents[1]/'LICENSE').read_bytes())
print('Created self-contained local PlayerChrome.html — no runtime npm or Python required')
