"""Complete black delivery preservation/budget check; --qt requires actual Qt."""
from pathlib import Path
import re
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools/player-chrome'),str(ROOT/'tools/sidebar-panel'),str(ROOT/'tests')]
from build_sidebar_black import monochrome
from build_external import qcompress
from sidebar_inline_test import native_bodies
before=(ROOT/'downloads/Sidebar-Compact/PlrSidebar.qml').read_text()
path=ROOT/'downloads/Player-Chrome-Monochrome/PlrSidebar.qml'
after=path.read_text()
assert monochrome(before)==after
assert native_bodies(before)==native_bodies(after)
def model(text):
    start=text.index('property list<QtObject> pAllSidebarItems')
    return text[start:text.index('property ',text.index(']\n',start)+2)]
assert model(before)==model(after)
assert before.count('MouseArea {')==after.count('MouseArea {')
assert len(path.read_bytes())==80039
assert len(qcompress(path.read_bytes()))==15408<=15868
print(f'PASS: complete black sidebar, 15408/15868, {len(native_bodies(after))} native bodies and model unchanged')
if '--qt' in sys.argv:
    from PySide6.QtCore import QByteArray,qCompress,qUncompress
    data=path.read_bytes()
    compressed=qCompress(QByteArray(data),compressionLevel=9)
    assert len(compressed)==15408
    assert bytes(qUncompress(compressed))==data
    print('PASS: actual Qt qCompress level9 / full-file qUncompress roundtrip')
