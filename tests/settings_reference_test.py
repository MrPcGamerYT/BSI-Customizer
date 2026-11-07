"""Source/bridge contracts for the reference skin. Qt runtime tested separately."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'assets/emulator-settings/ReferencePanel.qml').read_text()
assert s==(ROOT/'downloads/Settings-Reference-MSI5/Assets/BSISettingsOutline.qml').read_text()
for token in ['property var bsiHost','property var bsiTheme','property alias pageContainer: bsiPageBody',
 'readonly property int selectedIndex: bsiHost ? bsiHost.pCurrentTabIndex : 0',
 'model: shell.bsiTabs','property var nativeEntry: model.modelData',
 'onClicked: if (shell.bsiHost) shell.bsiHost.fChangeSelectedSetting(index)',
 'shell.bsiHost.close()','shell.bsiHost.startSystemMove()',
 'width < 720','case "cube":','case "gear":','case "theme":',
 'nativeEntry.pButtonText','tabLabel.truncated','pShowRedDot']:
 assert token in s,token
for token in ['confCommit(', 'confDiscard(', 'setSource(', 'ComboBox {','Slider {','StackLayout {',
 'pCurrentTabIndex =','selectedIndex =','Save changes','BootScreen','FontLoader','UiTheme.']:
 assert token not in s,token
# No fabricated list entries or mutation of the native model.
assert not re.search(r'\{\s*pButtonId\s*:',s)
assert 'bsiHost.pSettingsItems.pSettingElements : []' in s
assert s.count('property alias pageContainer:')==1
assert s.count('onClicked:')==2
assert s.count('MouseArea {')==1 # Header dragging only, no overlays stealing native page clicks.
print('PASS: reference asset/API equality; native-only tabs, readonly selection, guarded callbacks, no dummy Save/pages or global theme/font mutation')
