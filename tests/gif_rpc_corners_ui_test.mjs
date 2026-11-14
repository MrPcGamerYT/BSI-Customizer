import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

// Exercise the actual page handlers, not a second implementation of the toggle.
const qml = fs.readFileSync(new URL('../src/qml/pages/GifRpcPage.qml', import.meta.url), 'utf8');
const setCorners = qml.match(/function setCorners\(enabled\) \{([\s\S]*?)\n    \}/)[1];
const configuration = qml.match(/function configuration\(\) \{([\s\S]*?)\n    \}/)[1];
const replies = [];
let accepted = true;
const ctx = {
  page: { cornerPreference: -1, show: r => replies.push(r) },
  backend: { gifRpcSetRoundCorners: (path, enabled) => ({ ok: accepted, msg: path, roundCorners: enabled }) },
  target: { text: 'C:/Player/HD-Player.exe' },
  rpcEnabled: { checked: false }, timestamp: { checked: true }, rotation: { value: 5 }
};
for (const name of ['appId', 'stateField', 'detailField', 'largeImage', 'largeText', 'smallImage', 'smallText', 'button1', 'url1', 'button2', 'url2']) ctx[name] = { text: '' };
Object.defineProperty(ctx, 'cornerPreference', { get: () => ctx.page.cornerPreference });
vm.createContext(ctx);
vm.runInContext(`function setCorners(enabled){${setCorners}};function configuration(){${configuration}}`, ctx);
const configValue = key => vm.runInContext(`configuration().${key}`, ctx);
assert.equal(configValue('RoundCorners'), undefined, 'unset preference leaves existing native corners alone');
vm.runInContext('setCorners(true)', ctx);
assert.equal(ctx.page.cornerPreference, 1);
assert.equal(configValue('RoundCorners'), true);
accepted = false;
vm.runInContext('setCorners(false)', ctx);
assert.equal(ctx.page.cornerPreference, 1, 'failed writes must not claim the preference changed');
accepted = true;
vm.runInContext('setCorners(false)', ctx);
assert.equal(ctx.page.cornerPreference, 0);
assert.equal(configValue('RoundCorners'), false);
assert.equal(configValue('Enabled'), false, 'changing corners never enables Discord RPC');
assert.equal(replies.length, 3);
assert.ok(qml.includes('page.configuration(), replaceExisting.checked'), 'Apply and profile save use the same configuration');
assert.ok(qml.includes('onExePathChanged() { page.cornerPreference = -1; }'), 'changing players clears the displayed saved preference');
console.log('PASS: round-corner enable/disable, unchanged defaults, failed writes, profile booleans and RPC independence.');
