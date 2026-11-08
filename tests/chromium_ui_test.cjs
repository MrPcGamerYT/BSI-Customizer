/* Browser-only UI regression with an explicit test double of the desktop
 * transport. This does NOT certify a Qt/C++ build or an actual EXE patch.
 * npm install --prefix .cache/web-test playwright
 * BSI_PLAYWRIGHT=../.cache/web-test/node_modules/playwright node tests/chromium_ui_test.cjs
 * Optional: BSI_CHROMIUM_EXECUTABLE and BSI_CHROMIUM_ARGS (JSON array).
 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const { chromium } = require(process.env.BSI_PLAYWRIGHT || 'playwright');
const root = path.resolve(__dirname, '../src/web');
const server = http.createServer((req, res) => {
  const name = new URL(req.url, 'http://test').pathname.slice(1) || 'index.html';
  if (!['index.html', 'styles.css', 'transport.js', 'editors.js', 'maintenance.js', 'resource-search.js', 'effects.js', 'app.js', 'icon.png', 'fonts/manrope-latin.woff2', 'fonts/plus-jakarta-sans-latin.woff2'].includes(name)) { res.writeHead(404).end(); return; }
  res.setHeader('Content-Type', name.endsWith('.js') ? 'text/javascript' : name.endsWith('.css') ? 'text/css' : name.endsWith('.woff2') ? 'font/woff2' : name.endsWith('.png') ? 'image/png' : 'text/html');
  res.end(fs.readFileSync(path.join(root, name)));
});
(async () => {
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const browser = await chromium.launch({ headless: true,
    ...(process.env.BSI_CHROMIUM_EXECUTABLE ? { executablePath: process.env.BSI_CHROMIUM_EXECUTABLE } : {}),
    args: JSON.parse(process.env.BSI_CHROMIUM_ARGS || '[]') });
  try {
    const url = `http://127.0.0.1:${server.address().port}`;
    const offline = await browser.newPage({ viewport: { width: 1480, height: 920 } });
    const errors = [];
    offline.on('pageerror', e => errors.push(e.message));
    await offline.goto(url);
    await offline.locator('#preview-button').waitFor({ state: 'visible' });
    await offline.locator('#preview-button').click();
    assert.equal(await offline.locator('#spine .nav-label').first().evaluate(el => getComputedStyle(el).opacity), '0', 'Sidebar labels start hidden');
    await offline.locator('#spine [data-page="0"]').hover();
    await offline.waitForFunction(() => getComputedStyle(document.querySelector('#spine .nav-label')).opacity === '1');
    assert.ok(await offline.locator('.spine').evaluate(el => el.getBoundingClientRect().width > 200), 'Sustained hover expands the sidebar');
    await offline.locator('.hero-main').hover();
    for (let i = 0; i < 13; ++i) {
      await offline.locator(`#spine [data-page="${i}"]`).evaluate(el => el.click());
      assert.ok(await offline.locator('#main').innerText());
      assert.equal(await offline.locator('[data-action]:enabled').count(), 0, `Offline native action on page ${i}`);
      if (i === 8) {
        assert.equal(await offline.locator('#settings-dialog').evaluate(el => el.open), true, 'Settings opens as a modal rather than a sidebar page');
        assert.equal(await offline.locator('[data-settings-tab]').count(), 3, 'Settings keeps Appearance, Updates and About in the top tab row');
        await offline.locator('[data-settings-tab="about"]').click();
        assert.equal(await offline.locator('#about-profile-preview-host img').count(), 1, 'About profile centers the application icon');
        await offline.locator('#settings-close').click();
      }
    }
    await offline.locator('#spine [data-page="0"]').evaluate(el => el.click());
    if (process.env.BSI_WEB_SCREENSHOT) await offline.screenshot({ path: process.env.BSI_WEB_SCREENSHOT });
    assert.equal(await offline.locator('#main > .matrix > .tile').count(), 4, 'Reference dashboard remains an uncluttered 2x2 matrix');
    assert.equal(await offline.locator('#spine .icon svg').count(), 13, 'Sidebar retains page glyphs');
    assert.equal(await offline.locator('#spine .nav-label').count(), 13, 'Every destination is labeled');
    assert.equal(await offline.locator('#spine .nav-group').count(), 5, 'Navigation destinations are grouped');
    assert.equal(await offline.locator('#cmd').count(), 0, 'Duplicate quick navigation is removed');
    assert.equal(await offline.getByText('UI by @Floatz4Ever').count(), 0, 'Visible concept attribution is removed');
    assert.equal(await offline.locator('.orbs i').count(), 3);
    for (const size of [{width: 918, height: 646}, {width: 1100, height: 700}, {width: 1920, height: 1080}]) {
      await offline.setViewportSize(size);
      const visible = await offline.locator('#main > .matrix').boundingBox();
      assert.ok(visible.x >= 0 && visible.y >= 0 && visible.x + visible.width <= size.width + 1 && visible.y + visible.height <= size.height + 1);
      assert.equal(await offline.locator('#stage').evaluate(el => getComputedStyle(el).transform), 'none', 'Responsive layouts must not scale the whole interface down');
      assert.equal(await offline.locator('body').evaluate(el => getComputedStyle(el).fontSize), '12px', 'Text keeps its readable CSS size at compact window dimensions');
    }
    await offline.setViewportSize({width:918,height:646});
    await offline.locator('#spine [data-page="8"]').evaluate(el => el.click());
    const settingsBox = await offline.locator('#settings-dialog').boundingBox();
    assert.ok(Math.abs(settingsBox.width - 918 * 0.70) < 2, 'Settings dialog is about 70% of a compact workspace width');
    const settingsHead = await offline.locator('#settings-drag-handle').boundingBox();
    await offline.mouse.move(settingsHead.x + 120, settingsHead.y + settingsHead.height / 2);
    await offline.mouse.down();
    await offline.mouse.move(settingsHead.x + 155, settingsHead.y + settingsHead.height / 2 + 24);
    await offline.mouse.up();
    const movedSettings = await offline.locator('#settings-dialog').boundingBox();
    assert.ok(movedSettings.x > settingsBox.x + 20 && movedSettings.y > settingsBox.y + 10, 'Settings header drag moves the panel without losing it off-screen');
    await offline.locator('#settings-close').click();
    await offline.setViewportSize({width:1480,height:920});
    assert.deepEqual(errors, []);
    const page = offline;
    await page.mouse.move(700, 110);
    await page.waitForTimeout(700);
    await page.goto('about:blank');
    page.on('pageerror', e => errors.push(e.message));
    page.on('dialog', dialog => dialog.accept());
    await page.addInitScript(() => {
      // Accept DOM confirmation dialogs only in this isolated mock test page.
      window.__autoConfirm = true;
      new MutationObserver(() => { if(window.__autoConfirm) document.querySelector('#confirm-dialog[open] button[value="yes"]')?.click(); }).observe(document, {subtree:true,childList:true,attributes:true,attributeFilter:['open']});
      const signal = () => ({ handlers: [], connect(fn) { this.handlers.push(fn); }, emit(...args) { this.handlers.forEach(fn => fn(...args)); } });
      window.__calls = [];
      let profile = {}, operations = [];
      const state = { protocol: 1, authorized: true, busy: false, exeFound: true, exePath: 'C:\\fixture\\HD-Player.exe', version: 'test-only', resourceRevision: 0, workspaceRevision: 0, updates: { configured: true, state: 'idle' }, aboutProfile: { developerName: 'Fixture developer', tagline: 'Fixture tagline', links: [{kind:'github',title:'Source',url:'https://example.test/source'},{kind:'discord',title:'Community',url:'https://example.test/chat'},{kind:'youtube',title:'Video',url:'https://example.test/video'},{kind:'youtube',title:'More videos',url:'https://example.test/more'}] }, themeStatus: 'Fixture palette', themeMethod: 'qml' };
      const colors = [{ name: 'testAccent', value: '#123456', edit: '', purpose: 'Test fixture only' }];
      const svgPreview = 'data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCI+PGNpcmNsZSBjeD0iMTIiIGN5PSIxMiIgcj0iOCIvPjwvc3ZnPg==';
      const rows = Array.from({ length: 250 }, (_, index) => ({ index, path: `qml/fixture-${index}.qml`, kind: 'text', text: true, size: 100, stored: 80, format: 'QML' }));
      rows[248] = { index: 248, path: 'images/fixture-background.jpg', kind: 'image', text: false, size: 1234, stored: 1234, format: 'JPEG', thumbnail: 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/p6sAAAAASUVORK5CYII=' };
      rows[249] = { index: 249, path: 'images/fixture-sidebar.svg', kind: 'svg', text: true, size: 128, stored: 128, format: 'SVG', thumbnail: svgPreview };
      const remote = {
        stateChanged: signal(), windowModeChanged: signal(), logMessage: signal(), fileSelected: signal(), videoStatus: signal(),
        windowAction(action) { (window.__windowActions ||= []).push(action); },
        snapshot(callback) { callback({ ...state }); },
        requestFile(role) { const path = window.__nextFile || ''; window.__nextFile = ''; remote.fileSelected.emit(role, path); },
        invoke(method, args, callback) {
          window.__calls.push({ method, args });
          let value = null;
          if (method === 'readTheme') value = { entries: colors.map(c => ({ ...c })), hex: [], presets: [], method: 'qml' };
          if (method === 'setThemeEdit') colors[0].edit = args.value;
          if (method === 'scanResources') { ++state.resourceRevision; value = rows.length; }
          if (method === 'listResources') value = rows;
          if (method === 'readResource') value = args.index === 249
            ? { text: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><circle cx="12" cy="12" r="8"/></svg>', preview: svgPreview, note: '', budget: { known: true, capacity: 300, msg: 'SVG fixture only' } }
            : { text: 'Item { property string value: "<img src=x onerror=alert(1)>" }', preview: 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/p6sAAAAASUVORK5CYII=', budget: { known: true, capacity: 300, msg: 'Test only' } };
          if (method === 'saveResource') { ++state.resourceRevision; value = { ok: true, msg: 'Test fixture acknowledged save' }; }
          if (method === 'setAboutProfile') { state.aboutProfile = { ...args, links: args.links.map(link => ({...link})) }; value = true; }
          if (method === 'fpsPatch') value = { ok: false, msg: 'Fixture: target is unsupported; no write' };
          if (method === 'bootLogoPreview') value = { available: true, logo: svgPreview };
          if (method === 'bootPatch') value = { ok: true, msg: 'Built-in Aurora HTML boot animation installed; no supported completion gate was found; requested 4567 ms hold was not applied.' };
          if (method === 'readInterface') value = {
            removals: ['Fixture sidebar', 'Fixture banner'], layout: [{ id: 'test-layout', label: 'Fixture layout', supported: true, applied: false }],
            about: { supported: true, text: 'Original fixture About', showSections: true, links: [{ id: 'website', name: 'Website', label: 'Original label', url: 'https://example.test', hidden: false }] }
          };
          if (method === 'rpcidNames') value = { ok: true, entries: [{ key: 'ProductName', value: 'Fixture', max: 40 }] };
          if (method === 'gifRpcInspect') value = { ok: true, installed: true, settings: { AppId: '12345678901234567', State: 'Loaded fixture', Enabled: true, Interval: 5 } };
          if (method === 'loadGradients') value = { targets: [{ key: 'fixture-surface', label: 'Fixture surface', supportsOrientation: true }] };
          if (method === 'maintenance') {
            value = {ok:true,msg:'Native maintenance fixture'};
            if(args.action==='sidebar-read') value={supported:true,applied:true,buttons:[{key:'capture',label:'Screenshot',hidden:false},{key:'volume',label:'Volume',hidden:false}]};
            if(args.action==='debloat-read') value={state:{supported:true},options:[{id:'fixture-ads',label:'Disable ads',hint:'Fixture configuration option'}]};
            if(args.action==='android-scan') value={ok:true,apps:[{package:'com.fixture.ad',label:'Fixture ad',installed:true,debloatable:true,note:'Reviewed fixture'}],download:{size:'1 KB'}};
            if(args.action==='disk-find') value={path:'C:\\fixture\\Root.vhd'};
            if(args.action==='disk-enable') window.__diskWritable=true;
            if(args.action==='disk-info'||args.action==='disk-enable') value={ok:true,writable:!!window.__diskWritable};
            if(args.action==='disk-list') value={ok:true,path:args.innerPath,writable:!!window.__diskWritable,entries:Array.from({length:240},(_,i)=>({name:'entry-'+i,path:'/entry-'+i,dir:i===0,size:i}))};
          }
          if (method === 'readWorkspace') value = { profile, operations };
          if (method === 'workspace') {
            ++state.workspaceRevision;
            const { action, options } = args;
            value = { ok: true, msg: 'Workspace test fixture: ' + action };
            if (action === 'profile-section') profile[options.section] = options.value;
            if (action === 'profile-load') profile = { name: 'Imported fixture', boot: { holdMs: 4567 } };
            if (action === 'queue-theme') operations.push({ kind: 'theme' });
            if (action === 'clear' || action === 'apply') operations = [];
            if (action === 'dry-run') value = window.__failDryRun ? { ok: false, msg: 'Fixture dry run refused' } : { ok: true, outputSha256: 'fixture-output-sha256' };
          }
          if (method === 'videoControl' && args.action === 'play') remote.videoStatus.emit('Qt decoder status fixture — not emulator certification');
          if (method === 'signOut') { state.authorized = false; remote.stateChanged.emit({ protocol: 1, authorized: false }); }
          callback({ ok: true, value });
        }
      };
      window.__remote = remote;
      window.__setBusy = busy => { state.busy = busy; remote.stateChanged.emit({ ...state }); };
      window.qt = { webChannelTransport: {} };
      window.QWebChannel = function (_, ready) { ready({ objects: { bsi: remote } }); };
    });
    await page.goto(url);
    await page.locator('#connection').waitFor({ state: 'hidden' });
    const maxButton = page.locator('[data-window="max"]');
    await maxButton.evaluate(el => el.click());
    await page.waitForFunction(() => __windowActions?.includes('max'));
    await page.evaluate(() => __remote.windowModeChanged.emit(true));
    assert.equal(await maxButton.getAttribute('aria-label'), 'Restore down', 'Maximized window exposes a restore action');
    await page.evaluate(() => __setBusy(true));
    assert.equal(await page.locator('[data-window="min"]').isDisabled(), false, 'Minimize remains available during a native operation');
    assert.equal(await page.locator('[data-window="max"]').isDisabled(), false, 'Maximize/restore remains available during a native operation');
    assert.equal(await page.locator('[data-window="close"]').isDisabled(), true, 'Close stays guarded while native work is active');
    await page.evaluate(() => __setBusy(false));
    await page.locator('[data-window="max"]').evaluate(el => el.click());
    await page.evaluate(() => __remote.windowModeChanged.emit(false));
    assert.equal(await maxButton.getAttribute('aria-label'), 'Maximize', 'Restored window returns to the maximize affordance');
    await page.locator('[data-window="min"]').evaluate(el => el.click());
    await page.waitForFunction(() => __windowActions?.includes('min'));
    await page.locator('#spine [data-page="8"]').evaluate(el => el.click());
    await page.locator('[data-settings-tab="about"]').click();
    await page.locator('[data-about-field="developerName"]').fill('Updated Fixture Developer');
    await page.locator('[data-about-field="tagline"]').fill('A customized About panel');
    await page.locator('[data-about-link="0"] [data-about-field="title"]').fill('My project');
    await page.locator('[data-about-link="0"] [data-about-field="url"]').fill('https://example.test/my-project');
    await page.locator('[data-action="settings-about-save"]').evaluate(el => el.click());
    await page.waitForFunction(() => __calls.some(c => c.method === 'setAboutProfile'));
    const profileCall = await page.evaluate(() => __calls.findLast(c => c.method === 'setAboutProfile'));
    assert.equal(profileCall.args.developerName, 'Updated Fixture Developer');
    assert.equal(profileCall.args.tagline, 'A customized About panel');
    assert.equal(profileCall.args.links.length, 4, 'About profile always has four bounded link slots');
    assert.match(await page.locator('#about-profile-preview-host').innerText(), /Updated Fixture Developer/);
    await page.locator('[data-settings-tab="updates"]').click();
    assert.match((await page.locator('#settings-content').innerText()).toLowerCase(), /release & recovery/);
    await page.locator('[data-action="editor:updates-auto"]').evaluate(el => el.click());
    await page.waitForFunction(() => __calls.some(c => c.method === 'updates' && c.args.action === 'autoCheck'));
    await page.locator('[data-settings-tab="appearance"]').click();
    await page.locator('#settings-close').click();
    await page.locator('#spine [data-page="2"]').evaluate(el => el.click());
    await page.waitForTimeout(800);
    assert.equal(await page.evaluate(() => __calls.filter(c => c.method === 'scanResources').length), 0, 'Navigation must never scan');
    await page.locator('[data-action="scanResources"]').evaluate(el => el.click());
    await page.waitForFunction(() => __calls.some(c => c.method === 'scanResources') && __calls.some(c => c.method === 'listResources'));
    await page.locator('[data-resource="0"]').waitFor();
    assert.equal(await page.locator('[data-resource]').count(), 100, 'Resource DOM must be paginated');
    const svgListRow = page.locator('[data-resource="249"]');
    assert.equal(await svgListRow.isVisible(), true, 'All files puts SVG resources on the first page instead of hiding them behind the SVG filter');
    assert.ok((await svgListRow.locator('.resource-thumb').getAttribute('src')).startsWith('data:image/svg+xml;base64,'), 'SVG row shows its miniature preview before the filename');
    await svgListRow.evaluate(el => el.click());
    await page.locator('#resource-code').waitFor();
    assert.match(await page.locator('#resource-code').inputValue(), /<svg/);
    assert.equal(await page.locator('#resource-image').isVisible(), true, 'Selecting an SVG loads its large preview beside editable SVG source');
    assert.ok((await page.locator('#resource-image').getAttribute('src')).startsWith('data:image/svg+xml;base64,'));
    await page.locator('[data-resource="0"]').evaluate(el => el.click());
    await page.waitForFunction(() => __calls.filter(c => c.method === 'readResource').length >= 2);
    assert.equal(await page.evaluate(() => __calls.filter(c => c.method === 'scanResources').length), 1);
    await page.locator('#resource-filter').fill('fixture-23 qml');
    await page.locator('[data-resource="23"]').evaluate(el => el.click());
    await page.locator('#resource-code').waitFor();
    assert.equal(await page.locator('#resource-editor img').count(), 1, 'Text and safe preview can be displayed together');
    assert.equal(await page.locator('#resource-image').isVisible(), true, 'Raster preview remains visible beside source text');
    await page.locator('#resource-code').fill('Item { property string value: "edited fixture" }');
    await page.locator('[data-action="saveResource"]').evaluate(el => el.click());
    await page.waitForFunction(() => __calls.some(c => c.method === 'saveResource'));
    assert.equal(await page.evaluate(() => __calls.find(c => c.method === 'saveResource').args.text), 'Item { property string value: "edited fixture" }');
    await page.locator('#resource-filter').fill('fixture-23');
    await page.locator('#resource-kind').selectOption('2');
    assert.equal(await page.locator('[data-resource]').count(), 0, 'A kind filter narrows the current scan');
    await page.locator('[data-action="scanResources"]').evaluate(el => el.click());
    await page.waitForFunction(() => __calls.filter(c => c.method === 'scanResources').length === 2 && __calls.filter(c => c.method === 'listResources').length >= 3);
    await page.locator('[data-resource="0"]').waitFor();
    assert.equal(await page.locator('#resource-filter').inputValue(), '', 'A fresh scan clears stale search text');
    assert.equal(await page.locator('#resource-kind').inputValue(), '0', 'A fresh scan returns to All files');
    assert.equal(await page.locator('[data-resource]').count(), 100, 'All detected entries are restored after scanning');
    assert.equal(await page.evaluate(() => __calls.filter(c => c.method === 'scanResources').length), 2);
    await page.locator('#spine [data-page="1"]').evaluate(el => el.click());
    await page.locator('input.inp[data-color="0"]').fill('#ABCDEF');
    await page.locator('input.inp[data-color="0"]').press('Tab');
    await page.waitForFunction(() => __calls.some(c => c.method === 'setThemeEdit'));
    assert.equal(await page.evaluate(() => __calls.find(c => c.method === 'setThemeEdit').args.value), '#ABCDEF');
    await page.locator('#spine [data-page="3"]').evaluate(el => el.click());
    await page.locator('[data-action="fpsPatch"]').evaluate(el => el.click());
    await page.locator('#toast').filter({ hasText: 'unsupported; no write' }).waitFor();
    await page.locator('#spine [data-page="2"]').evaluate(el => el.click());
    assert.equal(await page.evaluate(() => __calls.filter(c => c.method === 'scanResources').length), 2, 'Returning to Resources must not scan');
    for (let i = 0; i < 13; ++i) {
      await page.locator(`#spine [data-page="${i}"]`).evaluate(el => el.click());
      assert.ok(await page.locator('#main').innerText());
      if (i === 8) await page.locator('#settings-close').click();
    }
    // New HTML editors must dispatch the original backend contracts rather
    // than opening a native-editor placeholder.
    const nav = async index => {
      await page.locator(`#spine [data-page="${index}"]`).evaluate(el => el.click());
      await page.waitForFunction(() => !document.body.classList.contains('busy'));
    };
    const edit = key => page.locator(`[data-editor-field="${key}"]`);
    const click = async name => { await page.locator(`[data-action="editor:${name}"]`).evaluate(el => el.click()); await page.waitForFunction(() => !document.body.dataset.confirming && !document.body.classList.contains('busy')); };
    const last = method => page.evaluate(name => __calls.filter(c => c.method === name).at(-1), method);
    await nav(6);
    await click('interface-load');
    await page.locator('[data-interface-tab="remove"]').evaluate(el => el.click());
    await edit('remove:0').check();
    await click('removals');
    assert.deepEqual((await last('applyRemovals')).args.names, ['Fixture sidebar']);
    await page.locator('[data-interface-tab="about"]').evaluate(el => el.click());
    await edit('about:text').fill('Edited About fixture');
    await page.waitForFunction(() => document.querySelector('#interface-about-preview')?.innerText.includes('Edited About fixture'));
    await edit('link:0:show').uncheck();
    await edit('link:0:label').fill('Changed label');
    await click('about-apply');
    const about = (await last('applyAboutPage')).args;
    assert.equal(about.text, 'Edited About fixture');
    assert.equal(about.links[0].hidden, true, 'Show checkbox must invert to native hidden flag');
    assert.equal(about.links[0].label, 'Changed label');
    await click('about-profile');
    assert.equal((await last('workspace')).args.options.section, 'about');
    await nav(7);
    await edit('rpc:newId').fill('12345678901234567');
    await click('rpc-dry');
    assert.equal((await last('rpcidReplace')).args.dryRun, true);
    await click('rpc-names');
    await edit('rpc:nameKey').selectOption('ProductName');
    await edit('rpc:name').fill('My player fixture');
    await click('rpc-name-dry');
    assert.deepEqual((await last('rpcidSetName')).args, { text: 'My player fixture', key: 'ProductName', dryRun: true });
    await nav(10);
    await click('gif-inspect');
    await edit('gif:State').fill('Edited presence fixture');
    await edit('gif:Details').fill('One\nTwo');
    await edit('gif:Timestamp').check();
    await click('gif-apply');
    const gif = (await last('gifRpcApply')).args;
    assert.equal(gif.settings.State, 'Edited presence fixture');
    assert.equal(gif.settings.Details, 'One\nTwo');
    assert.equal(gif.settings.Timestamp, true);
    assert.equal(gif.replaceExisting, false);
    await nav(5);
    await page.locator('#boot-hold').fill('4567');
    await click('boot-profile');
    assert.equal((await last('workspace')).args.options.value.holdMs, 4567);
    assert.match((await page.locator('.boot-preview').innerText()).trim(), /^Emulator is starting…$/, 'boot preview shows only the requested neutral status');
    assert.ok((await page.locator('.boot-logo').getAttribute('src')).startsWith('data:image/svg+xml;base64,'), 'boot preview uses the selected player’s real logo');
    assert.equal(await page.locator('.boot-ring').count(), 1, 'boot preview wraps the logo in a glowing animated loading ring');
    await page.locator('#boot-startup-text').fill('Evoria <is> booting & ready');
    assert.equal(await page.locator('.boot-status').innerText(), 'Evoria <is> booting & ready');
    await page.locator('[data-action="bootPatch"]').evaluate(el => el.click());
    await page.waitForFunction(() => __calls.some(c => c.method === 'bootPatch'));
    let bootCall = await last('bootPatch');
    assert.deepEqual(bootCall.args, { holdMs: 4567, mode: 'aurora', sourcePath: '', startupText: 'Evoria <is> booting & ready' });
    await page.locator('#toast').filter({ hasText: 'no supported completion gate' }).waitFor();
    assert.equal(await page.locator('#toast').getAttribute('data-kind'), 'warning', 'Installed boot screen with an unapplied hold is shown as a partial-success warning, not a red failure');
    await page.locator('#boot-mode').selectOption('orbit');
    assert.ok((await page.locator('.boot-preview').getAttribute('class')).includes('theme-orbit'), 'preset selection changes the animated preview theme');
    await page.locator('[data-action="bootPatch"]').evaluate(el => el.click());
    await page.waitForFunction(() => __calls.filter(c => c.method === 'bootPatch').length === 2);
    bootCall = await last('bootPatch');
    assert.deepEqual(bootCall.args, { holdMs: 4567, mode: 'orbit', sourcePath: '', startupText: 'Evoria <is> booting & ready' });
    await page.locator('#boot-mode').selectOption('video');
    await page.evaluate(()=>{window.__nextFile='C:\\fixture\\modern-boot.mp4';});
    await page.locator('[data-action="bootVideoBrowse"]').evaluate(el => el.click());
    assert.match(await page.locator('#boot-video-selected').innerText(), /modern-boot\.mp4/);
    assert.match((await page.locator('.boot-preview').innerText()).trim(), /^Evoria <is> booting & ready$/, 'custom-video preview retains the custom startup label');
    await page.locator('[data-action="bootPatch"]').evaluate(el => el.click());
    await page.waitForFunction(() => __calls.filter(c => c.method === 'bootPatch').length === 3);
    bootCall = await last('bootPatch');
    assert.deepEqual(bootCall.args, { holdMs: 4567, mode: 'video', sourcePath: 'C:\\fixture\\modern-boot.mp4', startupText: 'Evoria <is> booting & ready' });
    await nav(1);
    await click('gradient-load');
    await click('gradient-apply');
    assert.equal((await last('applyGradient')).args.key, 'fixture-surface');
    await nav(11);
    await click('ws-queue-theme');
    await click('ws-dry-run');
    await click('ws-apply');
    assert.equal((await last('workspace')).args.options.outputSha256, 'fixture-output-sha256');
    await click('ws-queue-theme');
    await page.evaluate(() => { window.__failDryRun = true; });
    await click('ws-dry-run');
    assert.equal(await page.locator('[data-action="editor:ws-apply"]').count(), 0, 'Failed dry run must revoke Apply');
    await page.evaluate(() => { window.__nextFile = 'C:\\fixture\\profile.json'; });
    await click('profile-load');
    assert.equal((await last('workspace')).args.action, 'profile-load');
    await nav(9);
    await click('update-download');
    assert.equal((await last('updates')).args.action, 'download');
    await click('update-apply');
    assert.equal((await last('updates')).args.action, 'apply');
    // Sidebar and all maintenance controls stay in the same HTML workspace.
    await nav(6);
    await page.locator('[data-interface-tab="layout"]').evaluate(el => el.click());
    await click('maint-sidebar-read');
    await page.locator('[data-action="editor:maint-sidebar-down"]').first().evaluate(el => el.click());
    await edit('maint:hide:volume').check();
    await click('maint-sidebar-apply');
    assert.deepEqual((await last('maintenance')).args.action, 'sidebar-read');
    let maintenanceCall = await page.evaluate(() => __calls.findLast(c=>c.method==='maintenance'&&c.args.action==='sidebar-apply'));
    assert.deepEqual(maintenanceCall.args.order,['volume','capture']);
    assert.deepEqual(maintenanceCall.args.hidden,['volume']);
    await nav(12);
    assert.equal(await page.evaluate(() => __calls.filter(c=>c.method==='maintenance'&&c.args.action==='android-scan').length),0,'No ADB scan on page entry');
    await edit('maint:instance').fill('Nougat32');
    await click('maint-debloat-read');
    await edit('maint:option:fixture-ads').check();
    await click('maint-debloat-apply');
    maintenanceCall = await last('maintenance');
    assert.equal(maintenanceCall.args.action,'debloat-read');
    assert.deepEqual(await page.evaluate(() => __calls.findLast(c=>c.method==='maintenance'&&c.args.action==='debloat-apply').args.ids), ['fixture-ads']);
    await page.locator('[data-maint-tab="android"]').evaluate(el => el.click());
    await click('maint-android-scan');
    assert.equal((await last('maintenance')).args.instance,'Nougat32');
    await edit('maint:package:com.fixture.ad').check();
    await click('maint-android-apply');
    maintenanceCall = await last('maintenance');
    assert.deepEqual(maintenanceCall.args.packages,['com.fixture.ad']);
    assert.equal(maintenanceCall.args.keepCopy,true);
    assert.equal(maintenanceCall.args.clearAppData,false);
    await page.setViewportSize({ width: 918, height: 646 });
    await page.locator('[data-maint-tab="disk"]').evaluate(el => el.click());
    const diskPathBox = await page.locator('.disk-open-controls .editor-field').boundingBox();
    const diskActionsBox = await page.locator('.disk-open-controls .button-grid').boundingBox();
    assert.ok(diskActionsBox.y >= diskPathBox.y + diskPathBox.height - 1, 'Compact Root image manager stacks path selection above its action buttons');
    assert.equal(await page.locator('#stage').evaluate(el => getComputedStyle(el).transform), 'none', 'Compact Root image manager keeps normal-sized content');
    await page.setViewportSize({ width: 1480, height: 920 });
    await click('maint-disk-find');
    await click('maint-disk-info');
    await page.locator('[data-action="editor:maint-disk-select"][data-index="0"]').evaluate(el => el.click());
    await edit('maint:name').fill('new-folder');
    const writeCount=await page.evaluate(()=>__calls.filter(c=>c.method==='maintenance'&&c.args.action==='disk-mkdir').length);
    assert.equal(await page.locator('[data-action="editor:maint-disk-mkdir"]').isDisabled(),true,'Read-only writes are visibly disabled');
    assert.equal(await page.evaluate(()=>__calls.filter(c=>c.method==='maintenance'&&c.args.action==='disk-mkdir').length),writeCount,'Read-only guard must not dispatch a write');
    await click('maint-disk-enable');
    await click('maint-disk-list');
    assert.equal(await page.locator('.disk-file-row').count(),200,'Bound disk DOM rows');
    await click('maint-disk-next');
    assert.equal(await page.locator('.disk-file-row').count(),40);
    await click('maint-disk-previous');
    await page.locator('[data-action="editor:maint-disk-select"][data-index="1"]').evaluate(el => el.click());
    await page.evaluate(()=>{window.__nextFile='C:\\fixture\\entry.bin';});
    await click('maint-disk-extract');
    maintenanceCall = await last('maintenance');
    assert.equal(maintenanceCall.args.innerPath,'/entry-1');
    assert.equal(maintenanceCall.args.file,'C:\\fixture\\entry.bin');
    await click('maint-disk-mkdir');
    assert.equal(await page.evaluate(()=>__calls.findLast(c=>c.method==='maintenance'&&c.args.action==='disk-mkdir').args.name),'new-folder');

    // Cancel is a real no-write path, not a test auto-accept assumption.
    await nav(1);
    await page.evaluate(()=>{window.__autoConfirm=false;});
    const gradientWrites=await page.evaluate(()=>__calls.filter(c=>c.method==='applyGradient').length);
    await page.locator('[data-action="editor:gradient-apply"]').evaluate(el => el.click());
    await page.locator('#confirm-dialog').waitFor({state:'visible'});
    await page.locator('#confirm-cancel').click();
    await page.waitForFunction(()=>!document.body.dataset.confirming);
    assert.equal(await page.evaluate(()=>__calls.filter(c=>c.method==='applyGradient').length),gradientWrites);
    await page.evaluate(()=>{window.__autoConfirm=true;});
    const firstColor=edit('gradient:first'), originalColor=await firstColor.inputValue();
    await firstColor.click();
    await page.locator('#cphex').fill('#123456');
    await page.locator('#color-dialog button[value="no"]').click();
    assert.equal(await firstColor.inputValue(),originalColor,'Picker cancellation must retain draft');
    await firstColor.click();
    await page.locator('#cphex').fill('#123456');
    await page.locator('#color-dialog button[value="yes"]').click();
    await page.waitForFunction(()=>document.querySelector('[data-editor-field="gradient:first"]').value==='#123456');
    assert.equal(await firstColor.inputValue(),'#123456');
    await firstColor.click();
    await page.locator('#cphue').focus();
    await page.keyboard.press('ArrowRight');
    assert.ok(Number(await page.locator('#cphue').getAttribute('aria-valuenow'))>=0);
    await page.locator('#color-dialog button[value="yes"]').click();
    await click('gradient-apply');
    assert.equal((await last('applyGradient')).args.first,await firstColor.inputValue());

    // HTML controls still address the original Qt video decoder.
    await nav(11);
    await page.evaluate(()=>{window.__nextFile='C:\\fixture\\boot.mp4';});
    await click('video-inspect');
    assert.equal(await page.locator('#video-host').count(),1);
    await page.locator('#video-host').scrollIntoViewIfNeeded();
    await page.waitForFunction(()=>__calls.some(c=>c.method==='videoControl'&&c.args.action==='geometry'&&c.args.visible));
    const viewport=await page.evaluate(()=>__calls.findLast(c=>c.method==='videoControl'&&c.args.action==='geometry'&&c.args.visible).args);
    assert.ok(viewport.clipHeight>0&&viewport.clipHeight<=viewport.height,'Native video obeys HTML viewport clipping');
    await click('video-play');
    assert.ok(await page.evaluate(()=>__calls.some(c=>c.method==='videoControl'&&c.args.action==='play')));
    assert.ok((await page.locator('#video-status').innerText()).includes('Qt decoder status fixture'));
    await click('video-pause');
    await click('video-crop');
    assert.ok(await page.evaluate(()=>__calls.some(c=>c.method==='videoControl'&&c.args.action==='crop'&&c.args.crop===false)));
    await nav(0);
    await page.evaluate(()=>{window.__effectStart=performance.now();window.__effectFired=0;BsiEffects.windowAction('min',()=>{window.__effectFired=performance.now();});});
    assert.ok(await page.locator('body').evaluate(el=>el.classList.contains('minimizing')));
    await page.waitForFunction(()=>window.__effectFired>0);
    assert.ok(await page.evaluate(()=>__effectFired-__effectStart>=140),'Minimize waits for its HTML animation');
    await page.waitForFunction(()=>!document.body.classList.contains('minimizing'));
    await page.emulateMedia({reducedMotion:'reduce'});
    const motionDelay=await page.evaluate(async()=>{const start=performance.now();await BsiEffects.windowAction('min',()=>{});return performance.now()-start;});
    assert.ok(motionDelay<140,'Reduced motion must skip the full window-animation delay');
    await page.emulateMedia({reducedMotion:'no-preference'});
    assert.equal(await page.evaluate(() => __calls.filter(c => c.method === 'openNativeTool').length), 0, 'Integrated editors must not use a native fallback as their main implementation');
    await page.locator('#tele [data-action="signOut"]').evaluate(el => el.click());
    await page.locator('#connection').waitFor({ state: 'visible' });
    assert.equal(await page.locator('[data-action]:enabled').count(), 0);
    assert.deepEqual(errors, []);
    console.log('PASS: 13 routes; picker commit/cancel, styled cancellation and reduced-motion window effects; sidebar/debloat/ADB/disk requests; Qt preview controls; offline native actions disabled; manual scans; pagination/search; safe text rendering; real request arguments; failed-engine errors; integrated About/removal/RPC/GIF/boot/gradient/workspace/updater contracts; sign-out lock. UI test transport only.');
  } finally { await browser.close(); }
})().then(() => server.close()).catch(error => { console.error(error); server.close(); process.exitCode = 1; });
