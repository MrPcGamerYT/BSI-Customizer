"""Drop-in visual API and unchanged compressed native bridge checks."""
from pathlib import Path
import re
import struct
import zlib
ROOT=Path(__file__).resolve().parents[1]
style=(ROOT/'assets/emulator-settings/MonochromePanel.qml').read_text()
for required in ['property var bsiHost','property var bsiTheme','property alias pageContainer: bsiPageBody',
                 'shell.bsiHost.fChangeSelectedSetting(index)','shell.bsiHost.close()',
                 'bsiSettingsShell','bsiSettingsTab_','bsiSettingsClose']:
    assert required in style, required
for forbidden in ['confCommit(', 'confDiscard(', 'setSource(', 'ComboBox {', 'Slider {', 'StackLayout {', 'pCurrentTabIndex =']:
    assert forbidden not in style, forbidden
assert 'text: tab.nativeEntry.pButtonText' in style
assert 'property var nativeEntry: model.modelData' in style
assert 'model: shell.bsiTabs' in style
assert 'case "performance"' in style
assert not re.search(r'\bUiTheme\.',style)
package=ROOT/'downloads/Settings-Monochrome-MSI5'
assert (package/'Assets/BSISettingsOutline.qml').read_text()==style
for name in ['bridge.qml.txt','UiSettingsWindow.qml']:
    assert (package/name).read_bytes()==(ROOT/'downloads/Settings-Outline-MSI5'/name).read_bytes()
b=(package/'UiSettingsWindow.qml').read_bytes()
payload=struct.pack('>I',len(b))+zlib.compress(b,9)
assert len(payload)==2362 and len(payload)<=2701
assert zlib.decompress(payload[4:])==b
print('PASS: monochrome drop-in API; native model/guards retained; no fake controls; unchanged full Settings bridge 2362/2701 bytes')
