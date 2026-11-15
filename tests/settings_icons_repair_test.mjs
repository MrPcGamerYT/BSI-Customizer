import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const root=new URL('../assets/settings-icons-repair/',import.meta.url);
function load(name){
 const context={Qt:{resolvedUrl:path=>path}};vm.createContext(context);
 vm.runInContext(fs.readFileSync(new URL(name,root),'utf8').replace(/^\.pragma library\s*/,''),context);return context;
}
const chrome=load('Shared-Recovery/Icons.js'),settings=load('BSISettingsIconsV3.js');
const original29=['back','close','collapse','eco','fullscreen','gamepad','help','home','install','keymap','macro','maximize','menu','minimize','mute','pin','recent','rotate','screenshot','settings','sync','volume','memory','record','media','location','shake','lock','more'];
assert.equal(Object.keys(chrome.shapes).length,57);
for(const key of original29){
 assert.ok(Object.hasOwn(chrome.shapes,key),key);
 const svg=chrome.svg(key,'#abc');assert.ok(svg.includes('stroke="#abc"'));
 assert.ok(!svg.includes('ICON_COLOR'));
 assert.ok(chrome.url(key,'#f4f4f7').startsWith('data:image/svg+xml;charset=utf-8,'));
}
assert.notEqual(chrome.svg('gamepad'),chrome.svg('controller'),'both supplied primary designs remain distinct');
assert.equal(chrome.svg('keyboard'),chrome.svg('keymap'));
assert.equal(chrome.svg('constructor'),chrome.svg('preferences'));
assert.equal(chrome.svg('back','bad'),chrome.svg('back','#ffffff'));
const names=['performance','display','graphics','gameSettings','devices','controller','preferences','smartphone','keyboard','harddrive','advanced','info','close','palette','headphones','optimizer'];
const outputs=new Set();
for(const name of names){
 const svg=settings.svg(name,'#f4f4f7');outputs.add(svg);
 assert.ok(svg.includes('stroke="#f4f4f7"'));
 assert.ok(fs.existsSync(new URL(settings.pngUrl(name),root)),name);
}
assert.equal(outputs.size,names.length);
assert.ok(!fs.existsSync(new URL('Icons.js',root)),'no accidental shared replacement in deployment root');
const qml=fs.readFileSync(new URL('SettingsShell.qml',root),'utf8');
assert.ok(qml.includes('import "BSISettingsIconsV3.js" as Icons'));
assert.ok(!qml.includes('import "Icons.js"'));
assert.ok(qml.includes('property bool bsiPreferPngIcons: true'));
assert.ok(qml.includes('shell.bsiHost.fChangeSelectedSetting(index)'));
assert.ok(qml.includes('shell.bsiHost.close()'));
assert.ok(qml.includes('property alias pageContainer: bsiPageBody'));
assert.ok(qml.includes('Icons.kindForTab(id, pageSource, label)'));
assert.ok(qml.includes('tab.nativeEntry.pQmlSource'));
assert.ok(qml.includes('tab.nativeEntry.pButtonText'));
assert.ok(qml.includes('BSI icons v3'));
assert.equal(settings.kindForTab('0', '', 'Performance'), 'performance');
assert.equal(settings.kindForTab('setting_2', '', 'Graphics'), 'graphics');
assert.equal(settings.kindForTab('7', '', 'Devices'), 'devices');
assert.equal(settings.kindForTab('7', '', 'Phone'), 'smartphone');
assert.equal(settings.kindForTab('unknown', '', 'VOID Optimizer'), 'optimizer');
assert.equal(settings.kindForTab(null, 'qrc:/Settings/PlrAudioSetting.qml', 'Localized'), 'headphones');
assert.equal(settings.kindForTab(null, 'C:\\Player\\PlrUserDataSetting.qml', 'Localized'), 'harddrive');
assert.equal(settings.kindForTab('display', 'PlrGamepadSetting.qml', 'Performance'), 'controller');
assert.equal(settings.kindForTab('about', '', 'Localized'), 'info');
assert.equal(settings.kindForTab('?', '', '?'), 'preferences');
assert.ok(!qml.includes('Button { text: "Save"'));
console.log('PASS: all 29 chrome keys + 28 supplied additions/aliases retained; 16 distinct isolated Settings PNG/SVG glyphs; native sources and opaque IDs resolve without position assumptions; no shared import or duplicate native controls.');
