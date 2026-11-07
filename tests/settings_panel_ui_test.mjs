import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const requests = [];
let panelState = { supported: true, applied: false, restorable: false, detail: 'Editable player QML sources found.' };
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
    if (method === 'applySettingsPanelRebuild') {
      panelState = { supported: true, applied: !!args.enabled, restorable: !!args.enabled, detail: 'Regression state.' };
      return { ok: true, msg: 'applied' };
    }
    return { ok: true };
  },
  chooseFile: async () => null,
  getState: () => ({
    authorized: true, busy: false, aboutProfile: {
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
assert.match(view, /compact 70% footprint/);
assert.match(view, /settings-rebuild-preview/);
assert.match(view, /settings-preview-tab/);
assert.match(view, /Test \/ Apply Settings panel rebuild/);
assert.doesNotMatch(view, /settings-panel-apply[^>]*disabled/);
assert.match(view, /Panel Test/);
assert.match(view, /Community/);
assert.match(view, /Restore original Settings panel/);
assert.match(view, /Native Settings tabs/);
assert.match(view, /Embedded compiled QML/);
panelState = { ...panelState, supported: false, embeddedQmlResourceCount: 12, qmlResourceSamples: ['plr.qml', 'UiSettingsWindow.qml'] };
await editors.action('interface-load', null, 6);
view = editors.render(6);
assert.match(view, /Not supported/);
assert.match(view, /Registered QML resources/);
assert.match(view, /UiSettingsWindow\.qml/);
assert.doesNotMatch(view, /settings-panel-apply[^>]*disabled/);
panelState = { ...panelState, supported: true };
await editors.action('interface-load', null, 6);

await editors.action('settings-panel-apply', null, 6);
assert.equal(JSON.stringify(requests.filter(x => x.method === 'applySettingsPanelRebuild').at(-1)?.args), '{"enabled":true}');
assert.equal(panelState.applied, true);
await editors.action('settings-panel-restore', null, 6);
assert.equal(JSON.stringify(requests.filter(x => x.method === 'applySettingsPanelRebuild').at(-1)?.args), '{"enabled":false}');
console.log('PASS: Chromium exposes a real Settings-panel apply/restore action and saved About profile');
