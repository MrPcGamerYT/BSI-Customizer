"""Replace ONLY the known inline visual with a passive, live-bound asset adapter."""
from pathlib import Path
import json,re,sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/sidebar-panel'))
from build_external import structure,qcompress
URL='file:///C:/Program%20Files/BlueStacks_msi5/Assets/BSISidebarVisual.qml'

def convert(source,url=URL):
 if not url.startswith("file:///") or not url.endswith("/BSISidebarVisual.qml"):
  raise ValueError("Expected an absolute local BSISidebarVisual.qml URL")
 code,pairs,_=structure(source)
 if not any(m in source for m in ('BSI SIDEBAR MONO V1 COMPACT','BSI SIDEBAR INLINE V1 COMPACT')):
  raise ValueError('Expected the complete known compact sidebar; no native-layout guessing')
 match=re.search(r'component\s+BsiSidebarInlineVisual\s*:\s*Item\s*{',code)
 if not match:raise ValueError('Missing known visual component')
 end=pairs[code.index('{',match.start())]+1
 fields={'mode':'"button"','elementId':'""','assetName':'""','hovered':'false','pressed':'false','available':'true','active':'false','animating':'false'}
 adapter='component BsiSidebarInlineVisual: Item {\nid: skin\n'
 for key,default in fields.items():adapter+='property '+('string' if default.startswith('"') else 'bool')+' '+key+': '+default+'\n'
 adapter+='readonly property bool known: visual.status === Loader.Ready && visual.item !== null && visual.item.known\n'
 adapter+='Loader {\nid: visual\nanchors.fill: parent\nComponent.onCompleted: setSource('+json.dumps(url)+', {\n'
 adapter+=',\n'.join(key+': Qt.binding(function() { return skin.'+key+' })' for key in fields)
 adapter+='\n})\n}\n}\n'
 result=source[:match.start()]+adapter+source[end:]
 result=result.replace('BSI SIDEBAR MONO V1 COMPACT — complete visual skin, no external files.','BSI SIDEBAR ANIMATED V2 COMPACT — passive visual asset; native input retained.')
 result=result.replace('BSI SIDEBAR INLINE V1 COMPACT — complete visual skin, no external files.','BSI SIDEBAR ANIMATED V2 COMPACT — passive visual asset; native input retained.')
 structure(result)
 return result

if __name__=='__main__':
 folder=ROOT/'downloads/Sidebar-Animated-MSI5';(folder/'Assets').mkdir(parents=True,exist_ok=True)
 result=convert((ROOT/'downloads/Player-Chrome-Monochrome/PlrSidebar.qml').read_text(encoding='utf-8'))
 size=len(qcompress(result.encode()))
 if size>15868:raise SystemExit(f'Refused {size}/15868')
 (folder/'PlrSidebar.qml').write_text(result,encoding='utf-8')
 (folder/'Assets/BSISidebarVisual.qml').write_bytes((ROOT/'assets/emulator-sidebar/AnimatedVisual.qml').read_bytes())
 (folder/'Assets/Icons.js').write_bytes((ROOT/'assets/emulator-settings/Icons.js').read_bytes())
 print(f'Created complete native-derived sidebar: {size}/15868 qCompress-compatible bytes (Qt check required)')
