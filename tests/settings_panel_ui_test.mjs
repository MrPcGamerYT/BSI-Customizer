import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const svgAsset = file => ({
  mime: 'image/svg+xml',
  data: Buffer.from(readFileSync(new URL(`../src/assets/settings-panel-icons/${file}.svg`, import.meta.url), 'utf8')
    .replace('<svg ', '<svg color="#e7e9f2" ')).toString('base64')
});
const graphicsId = 'uigraphicssetting.qml';
const devicesId = 'plraudiosetting.qml';
const defaults = { [graphicsId]: svgAsset('graphics'), [devicesId]: svgAsset('devices') };
const custom = { mime: 'image/svg+xml', data: Buffer.from('<svg xmlns="http://www.w3.org/2000/svg"><circle cx="12" cy="12" r="9" fill="#55aaff"/></svg>').toString('base64') };
const requests = [];
let profile = { hiddenTabs: [], tabIcons: {} };
let panelState = {
  supported: true, applied: false, restorable: false, detail: 'Editable player QML sources found.',
  defaultTabIcons: defaults,
  tabs: [
    { id: graphicsId, page: 'UiGraphicsSetting.qml', label: 'Graphics', index: 0 },
    { id: devicesId, page: 'PlrAudioSetting.qml', label: 'Devices', index: 1 },
    { id: 'future.qml', page: 'Future.qml', label: 'Future tab', index: 2 }
  ]
};
const fakeWindow = {};
const sandbox = {
  window: fakeWindow,
  document: { querySelector: () => null },
  BsiMaintenance: { create: () => ({
    render: () => '', sidebar: () => '', input: () => {}, selectTab: () => {}, reset: () => {}
  }) },
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
    if (method === 'readInterface') return {
      layout: [], removals: [], about: {}, settingsPanel: panelState
    };
    if (method === 'setSettingsPanelAsset') profile.tabIcons[args.id] = custom;
    if (method === 'clearSettingsPanelAsset') delete profile.tabIcons[args.id];
    if (method === 'saveSettingsPanelProfile') profile.hiddenTabs = args.hiddenTabs;
    if (method === 'applySettingsPanelRebuild') {
      panelState = { ...panelState, supported: true, applied: !!args.enabled, restorable: !!args.enabled, detail: 'Regression state.' };
      return { ok: true, msg: 'applied' };
    }
    return { ok: true };
  },
  chooseFile: async () => '/chosen/custom.svg',
  getState: () => ({
    authorized: true, busy: false, settingsPanelProfile: profile, aboutProfile: {
      developerName: 'Panel Test', links: [
        { title: 'Project', url: 'https://example.com' },
        { title: 'Community', url: 'https://example.org' }
      ]
    }
  }),
  getResources: () => [],
  invalidateResources: () => {},
  setBootHold: () => {},
  refreshPalette: async () => {},
  enabled: () => true
};

const editors = fakeWindow.BsiEditors.create(ctx);
await editors.action('interface-load', null, 6);
let view = editors.render(6);
assert.match(view, /Settings panel rebuild/);
editors.selectInterfaceTab('settings-panel');
view = editors.render(6);
assert.match(view, /Rebuild the BlueStacks Settings panel/);
assert.match(view, /center the native page content/);
assert.match(view, /settings-rebuild-preview/);
assert.match(view, /settings-preview-tab/);
assert.match(view, /Apply Settings panel rebuild/);
assert.match(view, /About page identity/);
assert.match(view, /Choose logo/);
assert.match(view, /Save Settings rebuild options/);
assert.doesNotMatch(view, /settings-panel-apply[^>]*disabled/);
assert.match(view, /Panel Test/);
assert.match(view, /Community/);
assert.match(view, /Restore original Settings panel/);
assert.match(view, /Native Settings tabs/);
assert.match(view, /Embedded compiled QML/);
assert.match(view, /bundled icon pack is selected by default/);
assert.match(view, /Pack default icon/);
assert.match(view, /Player default/); // Unknown native pages do not get a guessed icon.
assert.ok(view.includes(defaults[graphicsId].data));
assert.ok(view.includes(defaults[devicesId].data));
assert.deepEqual(profile.tabIcons, {}, 'rendering defaults must not seed or overwrite the user profile');
assert.ok(!requests.some(x => x.method === 'setSettingsPanelAsset' || x.method === 'applySettingsPanelRebuild'));

await editors.action('settings-panel-icon', { dataset: { tabId: graphicsId } }, 6);
view = editors.render(6);
assert.ok(view.includes(custom.data));
assert.ok(!view.includes(defaults[graphicsId].data));
assert.ok(view.includes(defaults[devicesId].data));
assert.match(view, /data-action="editor:settings-panel-icon-clear"[^>]*>Use default</);
assert.equal(JSON.stringify(requests.filter(x => x.method === 'setSettingsPanelAsset').at(-1).args),
  JSON.stringify({ kind: 'tab-icon', id: graphicsId, path: '/chosen/custom.svg' }));
await editors.action('settings-panel-icon', { dataset: { tabId: devicesId } }, 6);
await editors.action('settings-panel-icon-clear', { dataset: { tabId: graphicsId } }, 6);
view = editors.render(6);
assert.ok(view.includes(defaults[graphicsId].data));
assert.ok(view.includes(custom.data));
assert.deepEqual(Object.keys(profile.tabIcons), [devicesId], 'Use default must clear only the chosen override');
assert.ok(!requests.some(x => x.method === 'applySettingsPanelRebuild'), 'choosing or clearing artwork must not patch implicitly');

await editors.action('settings-panel-reset', null, 6);
view = editors.render(6);
assert.deepEqual(Object.keys(profile.tabIcons), []);
assert.ok(view.includes(defaults[graphicsId].data) && view.includes(defaults[devicesId].data));
assert.ok(!view.includes(custom.data), 'reset must restore pack icons, not leave stale custom previews');


view = editors.render(6);
assert.match(view, /settings-panel-link-remove/);
assert.match(view, /\+ Add button/);
assert.match(view, />Custom</);
await editors.action('settings-panel-link-add', null, 6);
view = editors.render(6);
assert.match(view, /settings-panel:link:2:kind/, 'Adding a button appends a custom row');
await editors.action('settings-panel-link-remove', { dataset: { linkIndex: '1' } }, 6);
view = editors.render(6);
assert.doesNotMatch(view, /settings-panel:link:2:kind/, 'Removing a button shrinks the row list');
await editors.action('settings-panel-save', null, 6);
const savedProfile = requests.filter(x => x.method === 'saveSettingsPanelProfile').at(-1);
assert.equal(savedProfile.args.links.length, 2, 'Saving keeps the edited custom button list');
assert.equal(savedProfile.args.links[1].kind, 'custom', 'New buttons default to the custom kind');
panelState = { ...panelState, supported: false, embeddedQmlResourceCount: 12, qmlResourceSamples: ['plr.qml', 'UiSettingsWindow.qml'] };
await editors.action('interface-load', null, 6);
view = editors.render(6);
assert.match(view, /Not supported/);
assert.match(view, /Verified QML resources/);
assert.match(view, /UiSettingsWindow\.qml/);
assert.doesNotMatch(view, /settings-panel-apply[^>]*disabled/);
panelState = { ...panelState, supported: true, defaultIconError: 'The bundled Settings-tab icon pack is missing or damaged.' };
await editors.action('interface-load', null, 6);
assert.match(editors.render(6), /The bundled Settings-tab icon pack is missing or damaged/);
panelState = { ...panelState, defaultIconError: '' };
await editors.action('interface-load', null, 6);

await editors.action('settings-panel-apply', null, 6);
assert.equal(JSON.stringify(requests.filter(x => x.method === 'applySettingsPanelRebuild').at(-1)?.args), '{"enabled":true}');
assert.equal(panelState.applied, true);
await editors.action('settings-panel-restore', null, 6);
assert.equal(JSON.stringify(requests.filter(x => x.method === 'applySettingsPanelRebuild').at(-1)?.args), '{"enabled":false}');
console.log('PASS: Chromium default pack previews, per-tab SVG overrides, Use default, reset and explicit Settings-panel apply/restore');
