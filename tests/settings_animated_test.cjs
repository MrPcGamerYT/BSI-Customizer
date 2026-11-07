// Source/JS contracts. Real SVG/QML animation runtime is covered separately.
const fs=require('node:fs'),assert=require('node:assert/strict'),vm=require('node:vm');
const lib=fs.readFileSync('assets/emulator-settings/Icons.js','utf8');
const c={};vm.createContext(c);vm.runInContext(lib.replace(/^\.pragma library\s*/,''),c);
const base=['back','close','collapse','eco','fullscreen','gamepad','help','home','install','keymap','macro','maximize','menu','minimize','mute','pin','recent','rotate','screenshot','settings','sync','volume','memory','record','media','location','shake','lock','more'];
assert.equal(base.length,29);
for(const name of base)assert.ok(c.shapes[name],name);
assert.equal(Object.keys(c.shapes).length,57);
for(const [name,shape]of Object.entries(c.shapes)){
 assert.match(shape,/viewBox="0 0 24 24"/);
 assert.ok(!/<(?:script|foreignObject|image|style|text)\b|(?:href|onload)=/i.test(shape));
 const custom=c.svg(name,'#AABBCC');assert.ok(custom.includes('stroke="#AABBCC"'));assert.ok(!custom.includes('ICON_COLOR'));
 assert.equal(decodeURIComponent(c.url(name,'#AABBCC').split(',')[1]),custom);
}
for(const color of ['url(https://bad)','red','" onload="bad',null])assert.ok(c.svg('performance',color).includes('stroke="#ffffff"'));
assert.equal(c.svg('__proto__','#fff'),c.svg('preferences','#fff'));
assert.equal(c.svg('constructor','#fff'),c.svg('preferences','#fff'));
const qml=fs.readFileSync('assets/emulator-settings/AnimatedPanel.qml','utf8');
assert.equal(qml,fs.readFileSync('downloads/Settings-Animated-MSI5/Assets/BSISettingsOutline.qml','utf8'));
assert.equal(lib,fs.readFileSync('downloads/Settings-Animated-MSI5/Assets/Icons.js','utf8'));
for(const token of ['import "Icons.js" as Icons','Icons.url(icon.kind, icon.stroke.toString())','readonly property int selectedIndex:','property alias pageContainer: bsiPageBody','shell.bsiHost.fChangeSelectedSetting(index)','shell.bsiHost.close()','property bool bsiAnimations: true','tab.down','hoverTwist','Behavior on height','selectionFlash'])assert.ok(qml.includes(token),token);
for(const token of ['selectedIndex =','pCurrentTabIndex =','confCommit(','confDiscard(','setSource(','ComboBox {','Slider {','Animation.Infinite','Timer {','UiTheme.'])assert.ok(!qml.includes(token),token);
assert.equal((qml.match(/MouseArea \{/g)||[]).length,1);
assert.equal((qml.match(/onClicked:/g)||[]).length,2);
console.log('PASS: all 29 supplied keys + 28 additions/aliases; safe SVG tint/URLs; full package equality; native-only callbacks/model, bounded animations, reduced-motion switch');
