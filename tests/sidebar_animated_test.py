"""Full native delivery/model preservation, budget and passive helper checks."""
from pathlib import Path
import re,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools/sidebar-panel'),str(ROOT/'tools/sidebar-animated'),str(ROOT/'tests')]
from build import convert
from build_external import structure,qcompress
from sidebar_inline_test import native_bodies
before=(ROOT/'downloads/Player-Chrome-Monochrome/PlrSidebar.qml').read_text()
folder=ROOT/'downloads/Sidebar-Animated-MSI5'
after=(folder/'PlrSidebar.qml').read_text()
assert after==convert(before)
assert native_bodies(before)==native_bodies(after)
def model(q):
 start=q.index('property list<QtObject> pAllSidebarItems')
 return q[start:q.index('property ',q.index(']\n',start)+2)]
assert model(before)==model(after)
def outside(q):
 code,pairs,_=structure(q)
 m=re.search(r'component\s+BsiSidebarInlineVisual\s*:\s*Item\s*{',code)
 end=pairs[code.index('{',m.start())]+1
 return q[:m.start()].replace('BSI SIDEBAR MONO V1 COMPACT — complete visual skin, no external files.','BSI SIDEBAR ANIMATED V2 COMPACT — passive visual asset; native input retained.')+'\n'+q[end:].lstrip('\n')
assert outside(before)==outside(after), 'Native code outside the visual component changed'
assert before.count('MouseArea {')==after.count('MouseArea {')
visual=(ROOT/'assets/emulator-sidebar/AnimatedVisual.qml').read_text()
assert (folder/'Assets/BSISidebarVisual.qml').read_text()==visual
assert (folder/'Assets/Icons.js').read_bytes()==(ROOT/'downloads/Settings-Animated-MSI5/Assets/Icons.js').read_bytes()
for forbidden in ['MouseArea','HoverHandler','TapHandler','onClicked:','plrBackend','uiBackend']:
 assert forbidden not in visual,forbidden
assert 'settings:"advanced"' in visual
assert 'property bool motionEnabled: true' in visual
assert 'skin.animating && skin.available' in visual
assert 'Qt.binding(function() { return skin.pressed })' in after
assert 'visual.status === Loader.Ready' in after
assert len(qcompress(after.encode()))==13996<=15868
print(f'PASS: complete delivery, {len(native_bodies(after))} native bodies/model and ALL code outside visual unchanged; passive black SVG helper, same shared Icons.js; 13996/15868 zlib-compatible budget')
if '--qt' in sys.argv:
 from PySide6.QtCore import QByteArray,qCompress,qUncompress
 data=(folder/'PlrSidebar.qml').read_bytes();c=qCompress(QByteArray(data),compressionLevel=9)
 assert len(c)==13996 and bytes(qUncompress(c))==data
 print(f'PASS: actual Qt compression {len(c)}/15868, 1872 spare; complete-file roundtrip exact; {len(data)} UTF8 source bytes')
