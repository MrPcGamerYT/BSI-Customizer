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
for (const asset of ['styles.css', 'transport.js', 'resource-search.js', 'emulator-icons.js', 'editors.js', 'maintenance.js', 'binary-lab.js', 'effects.js', 'app.js']) {
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
const originalPages = ['DashboardPage', 'ThemePage', 'ResourcesPage', 'FpsPage', 'IntegrityPage', 'BootVideoPage', 'UiPage', 'RpcidPage', 'AppearancePage', 'AboutPage', 'GifRpcPage', 'WorkspacePage', 'DebloatPage'];
for (const page of originalPages) assert.equal((nativeShell.match(new RegExp('\\b' + page + ' \\{', 'g')) || []).length, 1, 'Exactly one original editor: ' + page);
assert.ok(!nativeShell.includes('WebEngineView'), 'Normal app must not switch between duplicated editor implementations');
assert.ok(read('src/main.cpp').includes('const bool chromiumUi = !args.contains(QStringLiteral("--native-ui"))'));
console.log('PASS: native recovery shell retains its 13 original feature editors and backend contracts.');

// The ordinary deliverable must actually contain the HTML frontend, not merely
// hide missing-feature escape buttons over a native-only installer.
assert.ok(read('.github/workflows/release.yml').includes('-DBSI_CHROMIUM_UI=ON'));
assert.ok(read('scripts/Build-SingleExe.ps1').includes("src/chromium"));
assert.ok(read('scripts/Test-SingleExe.ps1').includes('--chromium-runtime-test'));
assert.ok(read('src/web/app.js').includes("['Debloat / Android / Root.vhd'"));
for (const operation of ['sidebar-read','sidebar-apply','debloat-read','debloat-apply','debloat-restore','android-read','android-scan','android-apply','android-restore','disk-find','disk-archive','disk-info','disk-enable','disk-list','disk-search','disk-extract','disk-add','disk-add-folder','disk-replace','disk-mkdir','disk-rename','disk-move','disk-copy','disk-remove','disk-undo','disk-discard']) {
  assert.ok(bridge.includes('"'+operation+'"'), 'Missing original engine adapter: '+operation);
  assert.ok(read('src/web/maintenance.js').includes("'"+operation+"'"), 'Missing HTML workflow: '+operation);
}
assert.ok(read('src/chromium/ChromiumMain.qml').includes('MediaPlayer {'));
assert.ok(read('src/web/editors.js').includes("'video-play'"));
assert.ok(read('src/web/transport.js').includes('videoStatus'));
console.log('PASS: normal HTML packaging, 13th maintenance area and original Qt decoder control contracts.');

assert.ok(html.includes('binary-lab.js'));
assert.ok(bridge.includes('QIODevice::NewOnly'), 'Binary exports must not truncate an existing file');
assert.ok(bridge.includes('text("sha256")!=report.value("sha256")'), 'Follow-up requests must reject stale inspected data');
assert.ok(!read('src/web/index.html').includes('Floatz'));
assert.ok(read('src/core/BinaryInspector.cpp').includes('resources.size()>=4096'));
console.log('PASS: read-only Binary Lab asset, stale-source guard, bounded resource traversal and new-file-only exports.');

const webApp=read('src/web/app.js');
for(const label of ['Dashboard','Color studio','Resources','Performance','Integrity & safety','Boot experience','Player interface','Discord presence','Settings','About & updates','Animated presence','Patch workspace','Debloat / Android / Root.vhd'])
  assert.ok(webApp.includes("['"+label+"'"),'Original section name retained: '+label);
console.log('PASS: all 13 original native section names retained alongside Binary Lab.');

assert.ok(bridge.includes('args.value("thumbnails", true)'));
assert.ok(bridge.includes('resourceHexPreviewAt(index)'));
assert.ok(webApp.includes('rasterPreview(r.thumbnail)'));
assert.ok(webApp.includes('selected.text?')&&webApp.includes('resource-inspector'));
const adb=read('src/core/AdbClient.cpp');
assert.ok(adb.includes('QueryFullProcessImageNameW'));
assert.ok(adb.includes('parent.compare(advertised'));
assert.ok(!adb.includes('findExecutable('),'Do not execute ADB from PATH');
for(const id of ['system-read','system-settings'])assert.ok(bridge.includes('"'+id+'"'));
for(const id of ['balancedfps','efficientfps'])assert.ok(read('src/core/DebloatPatcher.cpp').includes('"'+id+'"'));
const installer=read('scripts/Build-Installer.ps1');
for(const file of ['adb.exe','AdbWinApi.dll','AdbWinUsbApi.dll','NOTICE.txt']) {
  assert.ok(installer.includes("'"+file+"'"));
  assert.ok(read('packaging/installer/BSI-Customizer.iss').includes('AdbDirectory}\\'+file));
}
assert.ok(installer.includes('WaitForExit(120000)'));
assert.ok(read('.github/workflows/release.yml').includes('./scripts/Build-Installer.ps1'));
console.log('PASS: cached raster resource previews, guarded system actions, declared frame profiles, parent-validated ADB lookup and explicit installer sidecar allowlist.');
