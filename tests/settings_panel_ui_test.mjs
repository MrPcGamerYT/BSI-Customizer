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
let cleanup = { id: 'clean-player-ui', label: 'Clean player UI', supported: true, applied: true, needsUpdate: true,
  detail: 'Hide the three-line title-bar menu while keeping sidebar Settings accessible.' };
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
      layout: [{ ...cleanup }], removals: ['Remove title details', 'Sidebar Rotate'], about: {}, settingsPanel: panelState
    };
    if (method === 'applyLayoutTweak') cleanup = { ...cleanup, applied: args.enabled, needsUpdate: false };
    if (method === 'setSettingsPanelAsset') (args.kind === 'link-icon' ? (profile.linkIcons ||= {}) : profile.tabIcons)[args.id] = custom;
    if (method === 'clearSettingsPanelAsset') delete (args.kind === 'link-icon' ? (profile.linkIcons ||= {}) : profile.tabIcons)[args.id];
    if (method === 'saveSettingsPanelProfile') { profile.hiddenTabs = args.hiddenTabs; profile.links = args.links; const kept = new Set(args.links.map(row => row.id)); for (const id of Object.keys(profile.linkIcons || {})) if (!kept.has(id)) delete profile.linkIcons[id]; }
    if (method === 'applySettingsPanelRebuild') {
      panelState = { ...panelState, supported: true, applied: !!args.enabled, restorable: !!args.enabled, detail: 'Regression state.' };
      return { ok: true, msg: 'applied' };
    }
    return { ok: true };
  },
  chooseFile: async role => { requests.push({ method: 'chooseFile', args: { role } }); return '/chosen/custom.svg'; },
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
assert.match(view, /Update this tweak/);
assert.match(view, /three-line title-bar menu/);
assert.match(view, /sidebar Settings accessible/);
assert.doesNotMatch(view, /Restore this tweak/);
await editors.action('layout', { dataset: { index: '0' } }, 6);
assert.equal(JSON.stringify(requests.filter(x => x.method === 'applyLayoutTweak').at(-1).args),
  '{"id":"clean-player-ui","enabled":true}', 'Update must keep cleanup enabled, not restore all buttons');
assert.equal(cleanup.applied, true);
assert.match(editors.render(6), /Restore this tweak/);
await editors.action('layout', { dataset: { index: '0' } }, 6);
assert.equal(JSON.stringify(requests.filter(x => x.method === 'applyLayoutTweak').at(-1).args),
  '{"id":"clean-player-ui","enabled":false}', 'Restore is still available after successful migration');
editors.selectInterfaceTab('remove');
const removalView = editors.render(6);
assert.equal((removalView.match(/>Remove title details<\/label>/g) || []).length, 1);
assert.doesNotMatch(removalView, /Titlebar Version|Titlebar Instance Label/);
editors.input({ dataset: { editorField: 'remove:0' }, type: 'checkbox', checked: true });
await editors.action('removals', null, 6);
assert.equal(JSON.stringify(requests.filter(x => x.method === 'applyRemovals').at(-1).args),
  '{"names":["Remove title details"]}', 'one combined control dispatches one combined removal');
editors.selectInterfaceTab('settings-panel');
view = editors.render(6);
assert.match(view, /Rebuild the BlueStacks Settings panel/);
assert.match(view, /center the native page content/);
assert.match(view, /settings-rebuild-preview/);
assert.match(view, /settings-preview-tab/);
assert.match(view, /whole window uses the opaque native theme/);
assert.match(view, /glass is limited to the selected tab/);
assert.doesNotMatch(view, /Glass surfaces, selected tabs and native controls/);
const styles = readFileSync(new URL('../src/web/styles.css', import.meta.url), 'utf8');
// Match the final cascade rule, not a stale earlier illustration definition.
const frameRules = [...styles.matchAll(/\.settings-rebuild-preview\{([^}]+)\}/g)];
const opaqueFrame = frameRules.findLast(rule => rule[1].includes('background:'))?.[1];
assert.match(opaqueFrame, /background:var\(--bg1\)/);
assert.doesNotMatch(opaqueFrame, /gradient|inset/);
assert.match(styles, /\.settings-preview-tab\{border-radius:6px\}/);
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

// Rebuilt player About has variable rows, arbitrary website links and stable
// per-link artwork. It must never be repadded to four or alter own-About links.
view = editors.render(6);
assert.equal((view.match(/data-link-row=/g) || []).length, 2);
assert.match(view, /Website \/ custom/);
assert.match(view, /\+ Add link/);
for (let i = 0; i < 4; ++i) await editors.action('settings-panel-link-add', null, 6);
view = editors.render(6);
const ids = [...view.matchAll(/data-link-row="([^"]+)"/g)].map(match => match[1]);
assert.equal(ids.length, 6);
assert.equal(new Set(ids).size, 6);
for (const id of ids.slice(2)) {
  editors.input({ dataset: { editorField: `settings-panel:link:${id}:title` }, value: `Custom ${id}` });
  editors.input({ dataset: { editorField: `settings-panel:link:${id}:url` }, value: `https://example.net/${id}` });
}
await editors.action('settings-panel-link-icon', { dataset: { linkId: ids[2] } }, 6);
assert.equal(requests.filter(x => x.method === 'chooseFile').at(-1).args.role, 'settings-panel-link-icon');
assert.equal(JSON.stringify(requests.filter(x => x.method === 'setSettingsPanelAsset').at(-1).args),
  JSON.stringify({ kind: 'link-icon', id: ids[2], path: '/chosen/custom.svg' }));
assert.match(editors.render(6), /Custom link icon/);
await editors.action('settings-panel-link-remove', { dataset: { linkId: ids[0] } }, 6);
await editors.action('settings-panel-save', null, 6);
assert.equal(profile.links.length, 5);
assert.equal(profile.links[1].id, ids[2]);
assert.equal(profile.linkIcons[ids[2]].data, custom.data);
assert.equal(ctx.getState().aboutProfile.links.length, 2, 'player link edits must not touch Customizer own-About data');
for (const row of [...profile.links]) await editors.action('settings-panel-link-remove', { dataset: { linkId: row.id } }, 6);
await editors.action('settings-panel-save', null, 6);
assert.equal(profile.links.length, 0);
assert.deepEqual(Object.keys(profile.linkIcons), []);
assert.match(editors.render(6), /No links. Use \+ to add one/);
await editors.action('interface-load', null, 6);
assert.equal((editors.render(6).match(/data-link-row=/g) || []).length, 0, 'empty player link list must survive reload, not restore own-About defaults');

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
console.log('PASS: Chromium variable About add/remove/empty/reload, arbitrary website links and stable custom icon assets isolated from own-About; cleanup Update/Restore migration, one combined title-details control, default pack previews, per-tab SVG overrides, Use default, reset and explicit Settings-panel apply/restore');
