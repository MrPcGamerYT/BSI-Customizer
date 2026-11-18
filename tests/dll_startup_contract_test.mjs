// Wiring guards only; native file/loader and actual QML tests run separately.
import assert from 'node:assert/strict';
import fs from 'node:fs';
const read = p => fs.readFileSync(new URL('../' + p, import.meta.url), 'utf8');
const cmake = read('CMakeLists.txt');
for (const file of ['src/core/DllStartup.cpp', 'src/core/DllStartupImport.cpp',
  'src/qml/components/DllStartupPanel.qml', 'src/qml/pages/DllStartupPage.qml', 'tests/dll-startup/file_test.cpp']) {
  assert.ok(cmake.includes(file), 'Shipping build must include ' + file);
}
const backend = read('src/core/Backend.cpp');
assert.match(read('src/core/Backend.h'), /\bvoid\s+invalidatePlayerAnalysis\(\)\s*;/,
  'Shared cache invalidation must be declared as a Backend member as well as defined');
for (const method of ['dllStartupInspect', 'dllStartupApply', 'dllStartupRestore']) {
  const at = backend.indexOf('QVariantMap Backend::' + method + '(');
  assert.ok(at >= 0, method);
  const body = backend.slice(at, backend.indexOf('\n}\n', at));
  const guard = body.indexOf('if (actionsLocked()) return lockedMap();');
  const worker = body.indexOf('return runBackground(');
  assert.ok(guard >= 0 && worker > guard, method + ' must guard before any background file access');
  assert.ok(body.includes('!m_backgroundWorker'), method + ' must keep disk work off the GUI thread');
  if (method !== 'dllStartupInspect') {
    assert.ok(body.indexOf('isProcessRunning(') < body.indexOf('const auto r = DllStartup::'));
    assert.ok(body.includes('invalidatePlayerAnalysis()'));
  }
}
const engine = read('src/core/DllStartup.cpp');
assert.ok(!/\b(QProcess|LoadLibrary|GetProcAddress|system|popen)\s*\(/.test(engine), 'No helper/interpreter/payload execution');
assert.ok(engine.includes('setDirectWriteFallback(false)'));
assert.ok(engine.includes('QIODevice::NewOnly'));
assert.ok(engine.includes('#ifdef BSI_DLLSTARTUP_TESTING'));
assert.ok(!cmake.includes('PRIVATE BSI_DLLSTARTUP_TESTING'), 'No fault injection in production');
assert.ok(read('src/qml/pages/AddonsPage.qml').includes('DllStartupPanel {'));
const panel = read('src/qml/components/DllStartupPanel.qml');
assert.ok(!panel.includes('Component.onCompleted:'), 'No auto-inspection/apply on page entry');
assert.ok(panel.includes('text: "zlag.dll"') && panel.includes('text: "zlag_init"') && panel.includes('onTextChanged: panel.invalidate()'));
assert.ok(panel.includes('acknowledge.checked && panel.ready'));
assert.ok(panel.includes('onWorkspaceChanged() { panel.invalidate(); }'));
assert.ok(panel.includes('onExePathChanged() { panel.invalidate(); }'));
assert.ok(read('src/web/addons.js').includes('data-page="13"'));
assert.ok(read('src/chromium/ChromiumMain.qml').includes('"DebloatPage.qml", "DllStartupPage.qml"]'));
assert.ok(read('src/chromium/WebBridge.cpp').includes('page > 13'));
console.log('PASS: native regular-DLL writer is embedded, guarded, opt-in, runtime-free and shared by both frontends; production has no test fault injection');
