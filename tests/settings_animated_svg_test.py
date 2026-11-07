"""Verify supplied icon preservation and all SVGs with the real Qt image decoder."""
from pathlib import Path
import sys,json,subprocess,importlib.util
from xml.etree import ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('icons_builder',ROOT/'tools/settings-icons/build_icons.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
assert (ROOT/'assets/emulator-settings/Icons.js').read_text()==module.build()
script="const fs=require('fs'),vm=require('vm');const c={};vm.createContext(c);vm.runInContext(fs.readFileSync('assets/emulator-settings/Icons.js','utf8').replace(/^\\.pragma library\\s*/,''),c);console.log(JSON.stringify(c.shapes));"
shapes=json.loads(subprocess.check_output(['node','-e',script],cwd=ROOT,text=True))
for key,body in module.BASE.items():assert shapes[key]==module.svg(body),key
for key,svg in shapes.items():
 root=ET.fromstring(svg);assert root.attrib['viewBox']=='0 0 24 24'
 assert root.attrib['stroke']=='ICON_COLOR'
 for element in root.iter():assert element.tag.split('}')[-1] in {'svg','path','rect','circle'}
print(f'PASS: all {len(module.BASE)} supplied shapes preserved; {len(shapes)} valid self-contained SVG entries')
if '--qt' in sys.argv:
 from PySide6.QtGui import QImage
 for key,svg in shapes.items():
  image=QImage.fromData(svg.replace('ICON_COLOR','#ffffff').encode(),'svg')
  assert not image.isNull(),key
  assert any(image.pixelColor(x,y).alpha()>0 for x in range(image.width()) for y in range(image.height())),key
 print(f'PASS: real Qt SVG decoder renders nonempty images for all {len(shapes)} shapes/aliases')
