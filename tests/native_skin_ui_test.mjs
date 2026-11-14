import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const read = p => fs.readFileSync(new URL('../'+p,import.meta.url),'utf8');
const ctx={};vm.createContext(ctx);vm.runInContext(read('src/qml/SkinCatalog.js').replace('.pragma library',''),ctx);
assert.equal(ctx.packs.length,6);
for(let i=0;i<6;i++) {
 const p=ctx.create(i);assert.equal(p.schema,'bsi.skin');assert.equal(p.base,ctx.packs[i].id);
 p.name='My custom name';p.radius=19;assert.equal(ctx.create(i).name,ctx.packs[i].name,'drafts must not mutate the catalog');
}
const icons={};vm.createContext(icons);vm.runInContext(read('src/qml/Icons.js').replace('.pragma library',''),icons);
const a=decodeURIComponent(icons.icon(icons.dashboard,'#aabbcc','line'));
const b=decodeURIComponent(icons.icon(icons.dashboard,'#aabbcc','bold'));
assert.ok(a.includes("stroke-width='1.7'"));assert.ok(b.includes("stroke-width='2.8'"));assert.notEqual(a,b);
const cmake=read('CMakeLists.txt'),main=read('src/main.cpp'),qml=read('src/qml/pages/AddonsPage.qml');
assert.ok(cmake.includes('VERSION 4.0.1'));assert.ok(cmake.includes('"Build optional legacy Chromium workspace" OFF'));
assert.ok(main.includes('args.contains(QStringLiteral("--web-ui")) && !args.contains(QStringLiteral("--native-ui"))'));
assert.ok(read('.github/workflows/release.yml').includes('-DBSI_CHROMIUM_UI=OFF'));
for(const name of ['SkinProfiles.cpp','SkinPreview.qml','SkinCatalog.js'])assert.ok(cmake.includes(name));
assert.ok(qml.includes('onExePathChanged'));assert.ok(!qml.includes('onPathsChanged'));
for(const action of ['skin-save','skin-load','queue-skin','dry-run','apply'])assert.ok(qml.includes('"'+action+'"'));
assert.ok(!qml.includes('backend.scanResources('),'entry must never scan');
assert.ok(qml.includes('onWorkspaceChanged() { page.plan = {}; }'),'revision changes invalidate the plan');
const backend=read('src/core/Backend.cpp');assert.ok(backend.indexOf('if (actionsLocked()) return lockedMap();',backend.indexOf('QVariantMap Backend::workspace'))<backend.indexOf('action == QLatin1String("queue-skin")'));
assert.ok(backend.includes('m_resourceFileHash != QCryptographicHash::hash(bytes,QCryptographicHash::Sha256)'));
assert.ok(backend.includes('m_workspaceOperations=pending'),'custom skin batches commit to queue only after validation');
assert.ok(qml.includes('Deploy player GUI components and icon families'));
assert.ok(ctx.create(0).playerUi);
assert.ok(qml.includes('skin-plan'));
assert.ok(read('src/core/PlayerSkinCompiler.cpp').includes('No palette-only success was staged'));
const login=read('src/qml/LoginGate.qml');for(const call of ['auth.startDiscord()','auth.continueSaved()','auth.completeManual(','auth.forgetSaved()']) assert.ok(login.includes(call));
assert.ok(!login.includes('authorized = true'));assert.ok(read('src/qml/Theme.qml').includes('JetBrains Mono'));assert.ok(read('src/qml/Theme.qml').includes('Inter'));
console.log('PASS: native 4.0.1 defaults, six independent editable drafts, icon variants, authorization/manual callback retention, explicit mapping guard and stale-plan invalidation.');

const shell=read('src/qml/Main.qml'),lazy=read('src/qml/components/LazyFeature.qml');
assert.equal((shell.match(/LazyFeature \{/g)||[]).length,13,'all tools keep a lazy route');
assert.ok(lazy.includes('active: authorized && (currentPage === pageIndex || visited)'));
assert.ok(lazy.includes('asynchronous: true'));
assert.ok(!shell.includes('backend.detectInstalls()'),'installation detection must be explicit on Home');
assert.ok(!shell.includes('visible: backend.busy'),'no modal busy curtain');
assert.ok(!read('src/qml/pages/UiPage.qml').includes('Component.onCompleted: reloadAll()'));
assert.ok(!read('src/qml/pages/ThemePage.qml').includes('page.reload();\n    }'));
assert.ok(!read('src/qml/components/CollectionCard.qml').includes('scale:'));
assert.ok(read('src/qml/components/CollectionCard.qml').includes('y: card.lifted ? -2 : 0'));
for(const token of ['#0A0A0E','#111116','#17171D','#7C5CFF','#C8E86A']) assert.ok(read('src/qml/Theme.qml').includes(token));
assert.ok(qml.includes('selectedDesignInspector') && qml.includes('collectionBento'));
console.log('PASS: exact control-center tokens, lazy authenticated routes, no entry scans, nonmodal busy status and bounded card interaction motion.');

const settings=read('src/core/SettingsManager.cpp');
const startup=settings.slice(settings.indexOf('SettingsManager::SettingsManager'),settings.indexOf('void SettingsManager::refreshFpsPath'));
assert.ok(!startup.includes('findBlueStacks(') && !startup.includes('refreshFpsPath();'),'no install/DLL scans before first paint');
for(const method of ['layoutTweaks','sidebarButtonRows','aboutPageState','detectInstalls','useInstall']) {
 const text=backend.slice(backend.indexOf('Backend::'+method+'('));
 assert.ok(text.slice(0,600).includes('!m_backgroundWorker'),method+' must not analyze files on the GUI thread');
}
console.log('PASS: saved-settings startup is scan-free; explicit install/layout/About reads use the isolated worker.');

assert.ok(read('src/qml/components/ActionButton.qml').includes('button.accent ? Theme.window : Theme.text'),'white pill labels remain readable while disabled');
assert.ok(read('src/qml/components/LogPanel.qml').includes('logModel.clear()'),'expanded activity keeps the Clear workflow');

assert.ok(read('src/qml/components/CollectionCard.qml').includes('property bool lifted: cardHover.hovered'),'stationary hover bounds avoid edge lift/unlift feedback');

const backdrop=read('src/qml/components/ControlCenterBackdrop.qml');
assert.ok(backdrop.includes('Theme.backgroundMotion && !Theme.reduceMotion'));
assert.ok(backdrop.includes('animationAllowed && visible'));
assert.ok(!backdrop.includes('Timer {') && !backdrop.includes('onXChanged: requestPaint'));
assert.ok(shell.includes('window.active && window.visibility !== Window.Minimized'));
const strip=read('src/qml/components/LoadingStrip.qml');
assert.ok(strip.includes('busy && visible && animationAllowed && !Theme.reduceMotion'));
assert.ok(strip.includes('Theme.reduceMotion ? parent.width'));
assert.ok(read('src/core/ThemeManager.cpp').includes('appearance/reduceMotion'));
assert.ok(read('src/core/ThemeManager.cpp').includes('appearance/backgroundMotion'));
assert.ok(read('src/qml/pages/AppearancePage.qml').includes('reduceMotionToggle'));
console.log('PASS: persisted motion switches, inactive-window ambient guard, bounded cached background and nonmodal accessible loading strip.');

assert.ok(!shell.includes('label:"+"'), 'no duplicate lower Home shortcut');
assert.ok(!shell.includes('id: settingsButton'), 'no duplicate lower Settings shortcut');
assert.ok(!shell.includes('if(!navGroup.selected) window.navigatePage'), 'group headings do not duplicate the first child destination');
for (const name of ['TextField','TextArea','ComboBox','SpinBox','CheckBox','TabBar','TabButton']) {
 assert.ok(cmake.includes('Studio'+name+'.qml'));
 assert.ok(read('src/qml/components/Studio'+name+'.qml').includes('Theme.'));
}
console.log('PASS: unique sidebar destinations and shared rounded native form controls.');
