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
const bootDispatch = invoke.slice(invoke.indexOf('method == QLatin1String("bootPatch")'), invoke.indexOf('method == QLatin1String("purgePlayerCache")'));
for (const mode of ['aurora', 'orbit', 'pulse', 'solar', 'video']) assert.ok(bootDispatch.includes(`QLatin1String("${mode}")`), `Native bridge must accept the UI boot mode: ${mode}`);
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
for (const asset of ['styles.css', 'addons.js', 'transport.js', 'resource-search.js', 'editors.js', 'maintenance.js', 'effects.js', 'app.js']) {
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
assert.ok(read('src/main.cpp').includes('const bool chromiumUi = args.contains(QStringLiteral("--web-ui")) && !args.contains(QStringLiteral("--native-ui"))'));
console.log('PASS: optional Chromium workspace, local assets, bounded previews, exception boundary and renderer recovery guards.');

const nativeShell = read('src/qml/Main.qml');
const settingsPanel = read('src/qml/components/SettingsPanel.qml');
const featurePages = ['DashboardPage', 'ThemePage', 'ResourcesPage', 'FpsPage', 'IntegrityPage', 'BootVideoPage', 'UiPage', 'RpcidPage', 'GifRpcPage', 'WorkspacePage', 'DebloatPage'];
const settingsPages = ['AppearancePage', 'UpdatesPage', 'AboutPage'];
for (const page of featurePages) assert.equal((nativeShell.match(new RegExp('pages/' + page + '\\.qml', 'g')) || []).length, 1, 'Exactly one feature editor: ' + page);
for (const page of settingsPages) assert.equal((settingsPanel.match(new RegExp('pages/' + page + '\\.qml', 'g')) || []).length, 1, 'Exactly one settings tab: ' + page);
assert.ok(nativeShell.includes('SettingsPanel {') && nativeShell.includes('objectName: "settingsPanel"'), 'Settings panel is wired into the shell');
assert.ok(nativeShell.includes('navGroup.modelData.label') && nativeShell.includes('navGroup.modelData.children'), 'Primary navigation exposes searchable-by-eye labels grouped into sections');
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
assert.ok(read('src/main.cpp').includes('const bool chromiumUi = args.contains(QStringLiteral("--web-ui")) && !args.contains(QStringLiteral("--native-ui"))'));
console.log('PASS: native shell retains eleven feature editors; Appearance, Updates and About are single-instance settings tabs with drag support.');

// 4.0.1 ships the native QML frontend. Legacy Chromium stays explicitly opt-in.
assert.ok(read('.github/workflows/release.yml').includes('-DBSI_CHROMIUM_UI=OFF'));
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
console.log('PASS: native default packaging, optional Chromium maintenance area and original Qt decoder control contracts.');
const webApp = read('src/web/app.js'), webHtml = read('src/web/index.html');
assert.ok(!webHtml.includes('id="cmd"') && !webApp.includes("$('#cmd')"), 'Chromium shell must not duplicate navigation in a quick-link strip');
assert.ok(webApp.includes("['CUSTOMIZE', [1, 2, 3, 4, 5, 6]]") && webApp.includes('nav-group-label') && webApp.includes('nav-label'), 'Chromium destinations are visibly grouped and labeled');
assert.ok(webApp.includes("filter = ''; kind = 0; offset = 0") && webApp.includes("$('#resource-kind').value = '0'"), 'Every fresh Resources scan returns to All files');
assert.ok(webApp.includes('visual assets shown first') && webApp.includes('svg\\+xml') && webApp.includes('resource-thumb'), 'All files prioritizes image/SVG entries and renders bounded row thumbnails');
assert.ok(webApp.includes('hasPreview'), 'Selected SVG has a large preview beside editable source');
assert.ok(bridge.includes('metadata << item.toMap()') && !bridge.includes('row.remove("thumbnail")') && read('src/core/Backend.cpp').includes('maxThumbnails = 8 * 1024 * 1024'), 'Native scan-bounded thumbnails reach Chromium without unbounded image decoding');
assert.ok(webApp.includes('resource-detail-grid') && webApp.includes('resource-preview-panel') && webApp.includes('resource-code'), 'HTML resources provide a side-by-side preview and safe source editor');
assert.ok(webApp.includes("['All files', 'Pictures', 'SVG'"), 'The resource filter names the all-files state explicitly');
const webEditors = read('src/web/editors.js');
assert.ok(webEditors.includes('data-interface-tab') && webEditors.includes('About page') && webEditors.includes('aboutPreview'), 'Player Interface is grouped and has a live safe About preview');
assert.ok(webEditors.includes("'settings-panel'") && webEditors.includes("'settings-panel-apply'") && webEditors.includes("'settings-panel-restore'"), 'The Chromium Player Interface exposes native Settings-panel apply and restore actions');
assert.ok(webEditors.includes("'appearance'") && webEditors.includes('Player Appearance Studio') && webEditors.includes("'appearance-inspect'") && webEditors.includes("'appearance-stage'") && webEditors.includes("'appearance-dry'") && webEditors.includes("'appearance-apply'"), 'Player Appearance Studio uses the authenticated Workspace inspect/stage/dry-run/apply flow');
assert.ok(bridge.includes('"queue-appearance"') && bridge.includes('"appearance-inspect"') && read('src/core/CustomizationWorkspace.cpp').includes('PlayerAppearancePatcher::apply'), 'Appearance edits are native Workspace operations, not renderer-side QML surgery');
assert.ok(read('src/qml/pages/UiPage.qml').includes('Appearance studio') && read('src/qml/pages/UiPage.qml').includes('queue-appearance'), 'Native fallback exposes the same Player Appearance Studio staging path');
const nativeUiPage = read('src/qml/pages/UiPage.qml');
assert.ok(nativeUiPage.includes('Settings panel') && nativeUiPage.includes('applySettingsPanelRebuild')
          && nativeUiPage.includes('saveSettingsPanelProfile') && nativeUiPage.includes('settingsPanelRebuildState'),
          'Native Player interface keeps the Settings-panel rebuild controls the Chromium tab has');
assert.ok(bridge.includes('applySettingsPanelRebuild') && bridge.includes('"settingsPanel", m_backend.settingsPanelRebuildState()'), 'Native bridge reports and applies the real Settings-panel patcher');
assert.ok(webEditors.includes("'Apply Settings panel rebuild'") && webEditors.includes('embeddedQmlResourceCount') && webEditors.includes('settingsPanelManualTest'), 'Settings-panel apply stays clickable when detection fails and exposes embedded QML and manual-test diagnostics');
assert.ok(webApp.includes('routeSettingsPanelManualTest') && bridge.includes('m_settingsPanelManualTest'), 'Manual GUI mode opens the Settings-panel view and restricts native actions to that disposable-copy test');
const manualPanelScript = read('scripts/Test-SettingsPanel-Manual.ps1');
assert.ok(manualPanelScript.includes("'gui-sandbox'") && manualPanelScript.includes('--settings-panel-manual-test') && manualPanelScript.includes('-Wait -PassThru'), 'The manual GUI tester keeps a disposable player copy alive only while the focused test window is open');
const webMaintenance = read('src/web/maintenance.js');
assert.ok(webMaintenance.includes('data-maint-tab') && webMaintenance.includes('Root image file manager') && webMaintenance.includes('disk-manager') && webMaintenance.includes('disk-breadcrumbs'), 'Debloat modes are grouped and Root disk browsing uses a file-manager layout');
assert.ok(webMaintenance.includes("disk?.writable") && webApp.includes("el.dataset.locked === 'true'"), 'Disk writes remain visibly gated by native writable status');
const webStyles = read('src/web/styles.css');
assert.ok(webStyles.includes('transition:width .22s ease .5s') && !webHtml.includes('UI by @Floatz4Ever'), 'Chromium sidebar collapses to icons and removes the visible credit');
assert.ok(webStyles.includes('#stage{position:absolute;inset:0;width:100%;height:100%;min-width:0;min-height:0}') && !webApp.includes('innerWidth / 1480'), 'Chromium uses responsive layout instead of shrinking all UI to fit a fixed design canvas');
assert.ok(webStyles.includes('.disk-open-controls{grid-template-columns:minmax(0,1fr)}') && webStyles.includes('#toast[data-kind="warning"]'), 'Compact disk controls stack and partial boot results are visually distinguished');
assert.ok(webHtml.includes('id="settings-dialog"') && webHtml.includes('data-settings-tab="appearance"') && webApp.includes('settingsDrag = {') && webApp.includes("index === 8) { openSettings('appearance')"), 'Chromium settings is a movable dialog with top-level Appearance, Updates and About tabs');
assert.ok(bridge.includes('state.insert("aboutProfile"') && bridge.includes('method == QLatin1String("setAboutProfile")') && bridge.includes('m_settings.setAboutLinks(links)'), 'Custom About profile is persisted through the authorized native settings manager');
assert.ok(bridge.includes('links.size() != 4') && bridge.includes('QUrl::StrictMode') && bridge.includes('scheme != QLatin1String("https")'), 'About profile writes retain bounded four-link and strict HTTP(S) validation');
assert.ok(read('src/web/app.js').includes('about-profile-preview') && read('src/web/app.js').includes('settings-about-save') && read('src/web/editors.js').includes('settingsUpdates'), 'Settings dialog includes centered customizable About identity and separated update controls');
assert.ok(webApp.includes('transport.onWindowMode?.(updateWindowMode)') && webApp.includes("el.dataset.window === 'close'") && webApp.includes("button.title = windowMaximized ? 'Restore down' : 'Maximize'"), 'Window state controls restore affordance and remain usable while background work runs');
assert.ok(read('src/chromium/ChromiumMain.qml').includes('signal windowModeChanged(bool maximized)') && read('src/web/transport.js').includes('onWindowMode(fn)'), 'Native maximized/restored state is relayed to the Chromium controls');
assert.ok(read('src/qml/Main.qml').includes('WindowGeometry.initialGeometry') && read('src/chromium/ChromiumMain.qml').includes('WindowGeometry.initialGeometry'), 'native and Chromium shells use screen-aware centered startup geometry');
assert.ok(webApp.includes("'bootVideoBrowse'") && webApp.includes("mode, sourcePath: mode === 'video'") && ['aurora', 'orbit', 'pulse', 'solar'].every(theme => webApp.includes(`value=\"${theme}\"`)), 'Chromium boot editor offers four built-in animation themes and selected-video mode');
const bootPatcher = read('src/core/BootVideoPatcher.cpp');
assert.ok(bootPatcher.includes('QString htmlBootPage(const QString &logoUrl, bool withVideo, const QString &theme, const QString &startupText)') && bootPatcher.includes('R"HTML(') && bootPatcher.includes('Qt.resolvedUrl("BootScreen.html")') && bootPatcher.includes('Promo.mp4.bak'), 'Boot patcher writes a standalone HTML startup page under Assets and protects prior custom video');
assert.ok(['aurora', 'orbit', 'pulse', 'solar'].every(theme => bootPatcher.includes(`QLatin1String(\"${theme}\")`) || bootPatcher.includes(`QStringLiteral(\"${theme}\")`)) && bootPatcher.includes('theme-__BSI_BOOT_THEME__') && bootPatcher.includes('Choose Aurora, Orbit, Pulse, Solar or custom MP4 boot.'), 'native patcher validates and emits all four selectable standalone animation themes');
assert.ok(bootPatcher.includes('z:10000') && bootPatcher.includes('iBootProgressContainer') && bootPatcher.includes('iBootTipstimer'), 'Boot patcher foregrounds the replacement splash and removes the original progress overlay');
assert.ok(bootPatcher.includes('playerEmulatorLogo(exePath, original)') && bootPatcher.includes('Assets/BlueStacks.ico') && bootPatcher.includes('__BSI_EMULATOR_LOGO__') && !bootPatcher.includes('BSI  /  STARTUP') && !bootPatcher.includes('STARTING YOUR PLAYER'), 'generated boot screen embeds the selected player icon and adds no branding text');
assert.ok(read('src/chromium/WebBridge.cpp').includes('bootLogoPreview') && read('src/core/Backend.cpp').includes('BootVideoPatcher::logoPreviewDataUrl'), 'boot preview loads the real logo from the selected player');
assert.ok(read('src/qml/pages/BootVideoPage.qml').includes('Install Aurora Glow Boot') && webApp.includes('Install ${bootNames[bootMode]'), 'the themed boot patch action is available from both Boot Experience UIs');
assert.ok(bootPatcher.includes('sourceInfo.size() > maxVideoBytes') && bootPatcher.includes('sourceInfo.isSymLink()') && bootPatcher.includes('readBounded(source, maxVideoBytes'), 'Custom boot video path is size-bounded and refuses linked or oversized input files');
assert.ok(bootPatcher.includes('rollbackVideo') && bootPatcher.includes('rollbackHelper') && bootPatcher.includes('samePath(videoPath, movie)'), 'Boot asset changes roll back on patch failure and avoid copying the target onto itself');
assert.ok(bootPatcher.includes('requested %1 ms hold was not applied') && bootPatcher.includes('No extra boot hold was requested'), 'Boot result accurately distinguishes an unsupported requested hold from opting out');
assert.ok(read('src/chromium/WebBridge.cpp').includes('role == QLatin1String("boot-video")') && read('src/chromium/ChromiumMain.qml').includes('MP4 video (*.mp4)'), 'Chromium uses the authenticated native file dialog for MP4 selection');
console.log('PASS: Chromium shell, Resources, Player Interface, Root image manager and boot styles carry the UX improvements.');

assert.ok(bootDispatch.includes('text("startupText")'), 'boot message is sent through the native bridge');
assert.ok(bootPatcher.includes('startupText.toHtmlEscaped()') && bootPatcher.includes('qmlStringLiteral(startupText)') && bootPatcher.includes('textFormat: Text.PlainText'), 'boot message is escaped for HTML and QML and rendered as plain text');
