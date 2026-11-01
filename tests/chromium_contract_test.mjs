// Source/adapter consistency tests, not a substitute for native Windows tests.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const read = name => fs.readFileSync(new URL('../' + name, import.meta.url), 'utf8');
const bridge = read('src/chromium/WebBridge.cpp');
const test = read('src/chromium/ChromiumTests.cpp');
const commands = [...bridge.matchAll(/method == QLatin1String\("([^"]+)"\)/g)].map(m => m[1]);
const nativeGuardTests = test.slice(test.indexOf('const char *commands[]'), test.indexOf('for (const char *command'));
for (const command of commands) assert.ok(nativeGuardTests.includes('"' + command + '"'), 'Missing unauthenticated native regression: ' + command);
const invoke = bridge.slice(bridge.indexOf('QVariantMap WebBridge::invoke'));
assert.ok(invoke.indexOf('!m_auth.isAuthorized()') < invoke.indexOf('method == QLatin1String'));
assert.ok(invoke.indexOf('m_backend.busy() || m_dispatching') < invoke.indexOf('method == QLatin1String'));
assert.equal((bridge.match(/m_backend\.scanResources\(/g) || []).length, 1, 'Scan must have one explicit dispatch entry');
assert.ok(!bridge.slice(bridge.indexOf('QVariantMap WebBridge::snapshot'), bridge.indexOf('void WebBridge::publish')).includes('scanResources('));
assert.ok(bridge.includes('operation.remove("data")'), 'Do not serialize batch binary payloads into the UI');
const core = { };
vm.createContext(core); vm.runInContext(read('src/qml/ResourceSearch.js').replace('.pragma library', ''), core);
const web = { window: {} };
vm.createContext(web); vm.runInContext(read('src/web/resource-search.js'), web);
const rows = Array.from({ length: 1000 }, (_, index) => ({ index, path: `qml/Folder ${index % 8}/file-${index}.qml`, name: `file-${index}.qml`, kind: index % 3 ? 'text' : 'binary', text: !!(index % 3), format: index % 5 ? 'QML' : 'TTF', size: index * 100, modified: index % 7 === 0 }));
const nativeIndex = core.prepare(rows), webIndex = web.window.BsiResourceSearch.prepare(rows);
for (const query of ['', 'file-22', '"folder 2"', 'qml min:2kb max:40kb', 'Folder\\', 'min:1mb', '<script>']) {
  for (let kind = 0; kind < 7; ++kind) {
    const old = Array.from(core.filter(nativeIndex, query, kind), row => row.index);
    const current = Array.from(web.window.BsiResourceSearch.filter(webIndex, query, kind), row => row.index);
    assert.deepEqual(current, old, `Search behavior drift: ${query}, kind ${kind}`);
  }
}
const html = read('src/web/index.html'), cmake = read('CMakeLists.txt');
for (const asset of ['styles.css', 'transport.js', 'resource-search.js', 'editors.js', 'maintenance.js', 'effects.js', 'app.js']) {
  assert.ok(html.includes(asset), 'Missing browser asset: ' + asset);
  assert.ok(cmake.includes('src/web/' + asset), 'Missing embedded desktop asset: ' + asset);
}
assert.ok(!read('src/web/app.js').includes('This page has not yet been reimplemented'), 'Do not replace old tools with placeholder pages');
console.log(`PASS: ${new Set(commands).size} bridge commands covered by native locked-access test; manual scan dispatch; search/filter equivalence; embedded asset coverage.`);

// Downloadable bundles must receive the same locked sign-in policy as native
// releases; a renderer/bridge-only smoke test cannot detect a missing client ID.
const workflow = read('.github/workflows/chromium.yml');
assert.ok(workflow.includes('ZLP_AUTH_JSON: ${{ secrets.ZLP_AUTH_JSON || secrets.BSIC_AUTH_JSON }}'));
assert.ok(workflow.includes('-DBSI_REQUIRE_EMBEDDED_AUTH=ON'));
assert.ok(test.includes('const bool policyReady = auth.configured() && config.roleGateConfigured()'));
assert.ok(test.includes('!AuthManager::embeddedAuthJson().isEmpty() && config.clientSecret.isEmpty()'));
console.log('PASS: Chromium bundle embeds and verifies its locked Discord public-client policy.');

const shell = read('src/chromium/ChromiumMain.qml');
assert.ok(shell.includes('Qt.callLater(function()'), 'Unwind native calls before destroying the renderer on sign-out');
assert.ok(shell.includes('window.browserFailure = qsTr("The display process stopped'), 'Renderer failure must expose recovery UI');
assert.ok(shell.includes('window.retryBrowser()'), 'Renderer recovery must be retryable');
assert.ok(bridge.includes('catch (const std::bad_alloc &)'), 'Native allocation failures must not escape bridge invocation');
assert.ok(bridge.includes('8 * 1024 * 1024'), 'Inline resource previews must be bounded');
for (const asset of ['icon.png', 'fonts/manrope-latin.woff2', 'fonts/plus-jakarta-sans-latin.woff2']) {
  assert.ok(cmake.includes('src/web/' + asset), 'Missing embedded reference asset: ' + asset);
}
assert.ok(read('src/main.cpp').includes('const bool chromiumUi = !args.contains(QStringLiteral("--native-ui"))'));
console.log('PASS: optional Chromium workspace, local assets, bounded previews, exception boundary and renderer recovery guards.');

const nativeShell = read('src/qml/Main.qml');
const settingsPanel = read('src/qml/components/SettingsPanel.qml');
const featurePages = ['DashboardPage', 'ThemePage', 'ResourcesPage', 'FpsPage', 'IntegrityPage', 'BootVideoPage', 'UiPage', 'RpcidPage', 'GifRpcPage', 'WorkspacePage', 'DebloatPage'];
const settingsPages = ['AppearancePage', 'UpdatesPage', 'AboutPage'];
for (const page of featurePages) assert.equal((nativeShell.match(new RegExp('\\b' + page + ' \\{', 'g')) || []).length, 1, 'Exactly one feature editor: ' + page);
for (const page of settingsPages) assert.equal((settingsPanel.match(new RegExp('\\b' + page + ' \\{', 'g')) || []).length, 1, 'Exactly one settings tab: ' + page);
assert.ok(nativeShell.includes('SettingsPanel {') && nativeShell.includes('objectName: "settingsPanel"'), 'Settings panel is wired into the shell');
assert.ok(nativeShell.includes('navEntry.modelData.label') && nativeShell.includes('modelData.section'), 'Primary navigation exposes searchable-by-eye labels grouped into sections');
assert.ok(!nativeShell.includes('quickNav'), 'Do not duplicate the same destinations in a second navigation strip');
assert.ok(settingsPanel.includes('settingsPanelDragHandle') && settingsPanel.includes('mapToItem(panel.parent'), 'Settings header supports pointer-based dragging');
const resourcePage = read('src/qml/pages/ResourcesPage.qml');
assert.ok(resourcePage.includes('objectName: "resourceKindPicker"') && resourcePage.includes('qsTr("All files")'), 'Resource filters use a visible, explicit All files choice');
assert.ok(resourcePage.includes('page.kindFilter = 0') && resourcePage.includes('showing all supported entries'), 'A fresh resource scan returns to the complete list');
const playerUi = read('src/qml/pages/UiPage.qml');
assert.ok(playerUi.includes('objectName: "playerUiSections"') && playerUi.includes('About preview & editor'), 'Player UI options are grouped into findable sections');
assert.ok(playerUi.includes('page.previewLinks') && playerUi.includes('TextArea'), 'About editor has a live text-and-links preview beside its editor');
const debloatPage = read('src/qml/pages/DebloatPage.qml');
assert.ok(debloatPage.includes('objectName: "debloatModeTabs"') && debloatPage.includes('Root disk file manager'), 'Root disk manager is a primary, named debloat tab');
assert.ok(debloatPage.includes('page.mode = 1;') && debloatPage.includes('RootVhdBrowser {'), 'Opening an image switches directly to its file manager');
assert.ok(settingsPanel.includes('panel.dragOffsetX') && settingsPanel.includes('panel.dragOffsetY'), 'Settings dragging updates bounded panel offsets');
assert.ok(!nativeShell.includes('WebEngineView'), 'Normal app must not switch between duplicated editor implementations');
assert.ok(read('src/main.cpp').includes('const bool chromiumUi = !args.contains(QStringLiteral("--native-ui"))'));
console.log('PASS: native shell retains eleven feature editors; Appearance, Updates and About are single-instance settings tabs with drag support.');

// The ordinary deliverable must actually contain the HTML frontend, not merely
// hide missing-feature escape buttons over a native-only installer.
assert.ok(read('.github/workflows/release.yml').includes('-DBSI_CHROMIUM_UI=ON'));
assert.ok(read('scripts/Build-SingleExe.ps1').includes("src/chromium"));
assert.ok(read('scripts/Test-SingleExe.ps1').includes('--chromium-runtime-test'));
assert.ok(read('src/web/app.js').includes("['Debloat & Android'"));
for (const operation of ['sidebar-read','sidebar-apply','debloat-read','debloat-apply','debloat-restore','android-read','android-scan','android-apply','android-restore','disk-find','disk-archive','disk-info','disk-enable','disk-list','disk-search','disk-extract','disk-add','disk-add-folder','disk-replace','disk-mkdir','disk-rename','disk-move','disk-copy','disk-remove','disk-undo','disk-discard']) {
  assert.ok(bridge.includes('"'+operation+'"'), 'Missing original engine adapter: '+operation);
  assert.ok(read('src/web/maintenance.js').includes("'"+operation+"'"), 'Missing HTML workflow: '+operation);
}
assert.ok(read('src/chromium/ChromiumMain.qml').includes('MediaPlayer {'));
assert.ok(read('src/web/editors.js').includes("'video-play'"));
assert.ok(read('src/web/transport.js').includes('videoStatus'));
console.log('PASS: normal HTML packaging, 13th maintenance area and original Qt decoder control contracts.');
const webApp = read('src/web/app.js'), webHtml = read('src/web/index.html');
assert.ok(!webHtml.includes('id="cmd"') && !webApp.includes("$('#cmd')"), 'Chromium shell must not duplicate navigation in a quick-link strip');
assert.ok(webApp.includes("['CUSTOMIZE', [1, 2, 3, 4, 5, 6]]") && webApp.includes('nav-group-label') && webApp.includes('nav-label'), 'Chromium destinations are visibly grouped and labeled');
assert.ok(webApp.includes("filter = ''; kind = 0; offset = 0") && webApp.includes("$('#resource-kind').value = '0'"), 'Every fresh Resources scan returns to All files');
assert.ok(webApp.includes('resource-detail-grid') && webApp.includes('resource-preview-panel') && webApp.includes('resource-code'), 'HTML resources provide a side-by-side preview and safe source editor');
assert.ok(webApp.includes("['All files', 'Pictures', 'SVG'"), 'The resource filter names the all-files state explicitly');
const webEditors = read('src/web/editors.js');
assert.ok(webEditors.includes('data-interface-tab') && webEditors.includes('About page') && webEditors.includes('aboutPreview'), 'Player Interface is grouped and has a live safe About preview');
const webMaintenance = read('src/web/maintenance.js');
assert.ok(webMaintenance.includes('data-maint-tab') && webMaintenance.includes('Root image file manager') && webMaintenance.includes('disk-manager') && webMaintenance.includes('disk-breadcrumbs'), 'Debloat modes are grouped and Root disk browsing uses a file-manager layout');
assert.ok(webMaintenance.includes("disk?.writable") && webApp.includes("el.dataset.locked === 'true'"), 'Disk writes remain visibly gated by native writable status');
console.log('PASS: Chromium shell, Resources, Player Interface and Root image manager now carry the grouped UX improvements.');
