import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const requests = [];
let profile = {};
let operations = [];
const tokens = [
  { key: 'UiThemeObject.qml#root.fonts.bodyMedium.pixelSize', resource: 'UiThemeObject.qml', path: 'root.fonts.bodyMedium.pixelSize', label: 'Font size · bodyMedium', group: 'Typography', property: 'pixelSize', value: 13, minimum: 6, maximum: 72, step: 1 },
  { key: 'UiThemeObject.qml#root.buttons.sizes.large.radius', resource: 'UiThemeObject.qml', path: 'root.buttons.sizes.large.radius', label: 'Corner radius · large', group: 'Buttons', property: 'radius', value: 0, minimum: 0, maximum: 32, step: 1 },
  { key: 'plr.qml#root.pSidebarWidth', resource: 'plr.qml', path: 'root.pSidebarWidth', label: 'Sidebar width', group: 'Player chrome', property: 'pSidebarWidth', value: 32, minimum: 24, maximum: 96, step: 1 }
];
const fakeWindow = {};
const sandbox = {
  window: fakeWindow,
  document: { querySelector: () => null },
  BsiMaintenance: { create: () => ({ render: () => '', sidebar: () => '', input: () => {}, selectTab: () => {}, reset: () => {} }) },
  BsiEffects: { confirm: async () => true },
  console,
  setTimeout,
  clearTimeout,
  URL,
  URLSearchParams
};
vm.runInNewContext(readFileSync(new URL('../src/web/editors.js', import.meta.url), 'utf8'), sandbox);
const ctx = {
  esc: value => String(value ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c])),
  button: (label, action, _primary, extra = '') => `<button data-action="${action}" ${extra}>${label}</button>`,
  request: async (method, args = {}) => {
    requests.push({ method, args });
    if (method === 'readWorkspace') return { profile, operations };
    if (method === 'workspace') {
      const action = args.action;
      if (action === 'appearance-inspect') return { ok: true, msg: '3 tokens found.', tokens, resources: ['UiThemeObject.qml', 'plr.qml'] };
      if (action === 'queue-appearance') {
        operations = [{ kind: 'appearance', edits: args.options.edits }];
        return { ok: true, msg: 'Appearance edits staged.', operation: operations[0] };
      }
      if (action === 'dry-run') return { ok: true, msg: 'Verified isolated batch.', outputSha256: 'a'.repeat(64), steps: [] };
      if (action === 'apply') { operations = []; return { ok: true, msg: 'Batch committed.' }; }
      if (action === 'profile-section') { profile = { ...profile, [args.options.section]: args.options.value }; return { ok: true, msg: 'Profile section saved.' }; }
      if (action === 'profile-load') return { ok: true, profile };
    }
    return { ok: true };
  },
  chooseFile: async () => null,
  getState: () => ({ authorized: true, busy: false, workspaceRevision: 0, exePath: 'C:\\fixture\\HD-Player.exe' }),
  getResources: () => [],
  invalidateResources: () => {},
  setBootHold: () => {},
  refreshPalette: async () => {},
  enabled: () => true
};
const editors = fakeWindow.BsiEditors.create(ctx);
editors.selectInterfaceTab('appearance');
let view = editors.render(6);
assert.match(view, /Player Appearance Studio/);
assert.match(view, /Inspect appearance tokens/);
assert.match(view, /not a raw QML\/hex editor/);
assert.match(view, /appearance-apply[^>]*disabled/);

await editors.action('appearance-inspect', null, 6);
view = editors.render(6);
assert.match(view, /Typography/);
assert.match(view, /Buttons/);
assert.match(view, /Player chrome/);
assert.match(view, /data-editor-field="appearance:UiThemeObject\.qml#root\.buttons\.sizes\.large\.radius"/);
assert.match(view, /3 tokens found/);

editors.input({ dataset: { editorField: 'appearance:UiThemeObject.qml#root.buttons.sizes.large.radius' }, type: 'number', value: '8' });
editors.input({ dataset: { editorField: 'appearance:plr.qml#root.pSidebarWidth' }, type: 'number', value: '48' });
await editors.action('appearance-stage', null, 6);
const staged = requests.filter(r => r.method === 'workspace' && r.args.action === 'queue-appearance').at(-1);
assert.equal(JSON.stringify(staged.args.options.edits), JSON.stringify({
  'UiThemeObject.qml#root.buttons.sizes.large.radius': 8,
  'plr.qml#root.pSidebarWidth': 48
}));

await editors.action('appearance-dry', null, 6);
view = editors.render(6);
assert.doesNotMatch(view, /appearance-apply[^>]*disabled/);
await editors.action('appearance-apply', null, 6);
const applied = requests.filter(r => r.method === 'workspace' && r.args.action === 'apply').at(-1);
assert.equal(applied.args.options.outputSha256, 'a'.repeat(64));
view = editors.render(6);
assert.match(view, /Inspect the selected player to discover supported font/);

await editors.action('appearance-inspect', null, 6);
editors.input({ dataset: { editorField: 'appearance:UiThemeObject.qml#root.buttons.sizes.large.radius' }, type: 'number', value: '33' });
let refused = false;
try { await editors.action('appearance-stage', null, 6); } catch (error) { refused = /whole number from 0 to 32/.test(String(error.message || error)); }
assert.ok(refused, 'Out-of-range appearance values are refused before staging');

await editors.action('appearance-reset', null, 6);
await editors.action('appearance-profile', null, 6);
const saved = requests.filter(r => r.method === 'workspace' && r.args.action === 'profile-section').at(-1);
assert.equal(saved.args.options.section, 'appearance');
assert.equal(saved.args.options.value['UiThemeObject.qml#root.fonts.bodyMedium.pixelSize'], 13);
profile = { appearance: { 'UiThemeObject.qml#root.fonts.bodyMedium.pixelSize': 16 } };
await editors.action('profile-fields', null, 6);
view = editors.render(6);
assert.match(view, /value="16"/);
console.log('PASS: Chromium Player Appearance Studio inspect/stage/dry-run/apply/profile flow stays on the native Workspace bridge');
