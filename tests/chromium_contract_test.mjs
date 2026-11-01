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
for (const asset of ['styles.css', 'transport.js', 'resource-search.js', 'editors.js', 'app.js']) {
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
assert.ok(read('src/main.cpp').includes('const bool chromiumUi = args.contains(QStringLiteral("--web-ui"))'));
console.log('PASS: optional Chromium workspace, local assets, bounded previews, exception boundary and renderer recovery guards.');

const nativeShell = read('src/qml/Main.qml');
const originalPages = ['DashboardPage', 'ThemePage', 'ResourcesPage', 'FpsPage', 'IntegrityPage', 'BootVideoPage', 'UiPage', 'RpcidPage', 'AppearancePage', 'AboutPage', 'GifRpcPage', 'WorkspacePage', 'DebloatPage'];
for (const page of originalPages) assert.equal((nativeShell.match(new RegExp('\\b' + page + ' \\{', 'g')) || []).length, 1, 'Exactly one original editor: ' + page);
assert.ok(!nativeShell.includes('WebEngineView'), 'Normal app must not switch between duplicated editor implementations');
assert.ok(read('src/main.cpp').includes('&& !args.contains(QStringLiteral("--native-ui"))'));
console.log('PASS: one production shell retains all 13 original feature editors and backend contracts.');
