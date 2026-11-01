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
  if (!['index.html', 'styles.css', 'transport.js', 'editors.js', 'maintenance.js', 'binary-lab.js', 'resource-search.js', 'effects.js', 'app.js', 'icon.png', 'fonts/manrope-latin.woff2', 'fonts/plus-jakarta-sans-latin.woff2'].includes(name)) { res.writeHead(404).end(); return; }
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
    for (let i = 0; i < 14; ++i) {
      await offline.locator(`#spine [data-page="${i}"]`).click();
      assert.ok(await offline.locator('#main').innerText());
      assert.equal(await offline.locator('[data-action]:enabled').count(), 0, `Offline native action on page ${i}`);
    }
    await offline.locator('#spine [data-page="0"]').click();
    if (process.env.BSI_WEB_SCREENSHOT) await offline.screenshot({ path: process.env.BSI_WEB_SCREENSHOT });
    assert.equal(await offline.locator('#main > .matrix > .tile').count(), 4, 'Reference dashboard remains an uncluttered 2x2 matrix');
    assert.equal(await offline.locator('#spine .icon svg').count(), 14, 'Sidebar uses line icons, not numbered placeholders');
    assert.equal(await offline.locator('.orbs i').count(), 3);
    for (const size of [{width: 1100, height: 700}, {width: 1920, height: 1080}]) {
      await offline.setViewportSize(size);
      const visible = await offline.locator('#main > .matrix').boundingBox();
      assert.ok(visible.x >= 0 && visible.y >= 0 && visible.x + visible.width <= size.width + 1 && visible.y + visible.height <= size.height + 1);
    }
    assert.deepEqual(errors, []);
    await offline.close();

    const page = await browser.newPage({ viewport: { width: 1480, height: 920 } });
    page.on('pageerror', e => errors.push(e.message));
    page.on('dialog', dialog => dialog.accept());
    await page.addInitScript(() => {
      // Accept DOM confirmation dialogs only in this isolated mock test page.
      window.__autoConfirm = true;
      new MutationObserver(() => { if(window.__autoConfirm) document.querySelector('#confirm-dialog[open] button[value="yes"]')?.click(); }).observe(document, {subtree:true,childList:true,attributes:true,attributeFilter:['open']});
      const signal = () => ({ handlers: [], connect(fn) { this.handlers.push(fn); }, emit(...args) { this.handlers.forEach(fn => fn(...args)); } });
      window.__calls = [];
      let profile = {}, operations = [];
      const state = { protocol: 1, authorized: true, busy: false, exeFound: true, exePath: 'C:\\fixture\\HD-Player.exe', version: 'test-only', resourceRevision: 0, workspaceRevision: 0, updates: { configured: true, state: 'idle' }, themeStatus: 'Fixture palette', themeMethod: 'qml' };
      const colors = [{ name: 'testAccent', value: '#123456', edit: '', purpose: 'Test fixture only' }];
      const rows = Array.from({ length: 250 }, (_, index) => ({ index, path: `qml/fixture-${index}.qml`, kind: 'text', text: true, size: 100, stored: 80, format: 'QML' }));
      const remote = {
        stateChanged: signal(), logMessage: signal(), fileSelected: signal(), videoStatus: signal(),
        snapshot(callback) { callback({ ...state }); },
        requestFile(role) { const path = window.__nextFile || ''; window.__nextFile = ''; remote.fileSelected.emit(role, path); },
        invoke(method, args, callback) {
          window.__calls.push({ method, args });
          let value = null;
          if (method === 'readTheme') value = { entries: colors.map(c => ({ ...c })), hex: [], presets: [], method: 'qml' };
          if (method === 'setThemeEdit') colors[0].edit = args.value;
          if (method === 'scanResources') { ++state.resourceRevision; value = rows.length; }
          if (method === 'listResources') value = rows;
          if (method === 'readResource') value = { text: 'Item { property string value: "<img src=x onerror=alert(1)>" }', preview: '', budget: { known: true, capacity: 300, msg: 'Test only' } };
          if (method === 'saveResource') { ++state.resourceRevision; value = { ok: true, msg: 'Test fixture acknowledged save' }; }
          if (method === 'fpsPatch') value = { ok: false, msg: 'Fixture: target is unsupported; no write' };
          if (method === 'readInterface') value = {
            removals: ['Fixture sidebar', 'Fixture banner'], layout: [{ id: 'test-layout', label: 'Fixture layout', supported: true, applied: false }],
            about: { supported: true, text: 'Original fixture About', showSections: true, links: [{ id: 'website', name: 'Website', label: 'Original label', url: 'https://example.test', hidden: false }] }
          };
          if (method === 'rpcidNames') value = { ok: true, entries: [{ key: 'ProductName', value: 'Fixture', max: 40 }] };
          if (method === 'gifRpcInspect') value = { ok: true, installed: true, settings: { AppId: '12345678901234567', State: 'Loaded fixture', Enabled: true, Interval: 5 } };
          if (method === 'loadGradients') value = { targets: [{ key: 'fixture-surface', label: 'Fixture surface', supportsOrientation: true }] };
          if (method === 'binaryLab') {
            if(args.action==='inspect') value={ok:true,format:'PE32+',architecture:'x64',size:8192,sha256:'a'.repeat(64),entryRva:4096,sections:[{name:'.text',offset:1024,size:2048,virtualSize:2048,rva:4096,permissions:'XR-',valid:true}],imports:['KERNEL32.dll'],exports:[{name:'<img src=x onerror=alert(1)>',ordinal:1,rva:4096,offset:1024}],resources:[{path:'10/77/1033',offset:4096,size:8,codepage:65001}],overlaySize:4,overlayOffset:8188,warnings:[],note:'Fixture only'};
            if(args.action==='strings')value={ok:true,rows:[{offset:123,encoding:'ASCII',text:'<img src=x onerror=alert(1)>'}],note:'Fixture string result'};
            if(args.action==='hex')value={ok:true,text:'00000400  46 49 4c 45  FILE',next:args.offset+4096};
            if(args.action==='export')value={ok:true,msg:'Fixture exported without replacing source'};
          }
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
      window.qt = { webChannelTransport: {} };
      window.QWebChannel = function (_, ready) { ready({ objects: { bsi: remote } }); };
    });
    await page.goto(url);
    await page.locator('#connection').waitFor({ state: 'hidden' });
    await page.locator('#spine [data-page="2"]').click();
    assert.equal(await page.evaluate(() => __calls.filter(c => c.method === 'scanResources').length), 0, 'Navigation must never scan');
    await page.locator('[data-action="scanResources"]').click();
    await page.locator('[data-resource="0"]').waitFor();
    assert.equal(await page.locator('[data-resource]').count(), 100, 'Resource DOM must be paginated');
    assert.equal(await page.evaluate(() => __calls.filter(c => c.method === 'scanResources').length), 1);
    await page.locator('#resource-filter').fill('fixture-23 qml');
    await page.locator('[data-resource="23"]').click();
    await page.locator('#resource-code').waitFor();
    assert.equal(await page.locator('#resource-editor img').count(), 0, 'Resource source must not execute as HTML');
    await page.locator('#resource-code').fill('Item { property string value: "edited fixture" }');
    await page.locator('[data-action="saveResource"]').click();
    await page.waitForFunction(() => __calls.some(c => c.method === 'saveResource'));
    assert.equal(await page.evaluate(() => __calls.find(c => c.method === 'saveResource').args.text), 'Item { property string value: "edited fixture" }');
    await page.locator('#spine [data-page="1"]').click();
    await page.locator('input.inp[data-color="0"]').fill('#ABCDEF');
    await page.locator('input.inp[data-color="0"]').press('Tab');
    await page.waitForFunction(() => __calls.some(c => c.method === 'setThemeEdit'));
    assert.equal(await page.evaluate(() => __calls.find(c => c.method === 'setThemeEdit').args.value), '#ABCDEF');
    await page.locator('#spine [data-page="3"]').click();
    await page.locator('[data-action="fpsPatch"]').click();
    await page.locator('#toast').filter({ hasText: 'unsupported; no write' }).waitFor();
    await page.locator('#spine [data-page="2"]').click();
    assert.equal(await page.evaluate(() => __calls.filter(c => c.method === 'scanResources').length), 1, 'Returning to Resources must not scan');
    for (let i = 0; i < 14; ++i) {
      await page.locator(`#spine [data-page="${i}"]`).click();
      assert.ok(await page.locator('#main').innerText());
    }
    assert.ok(!(await page.locator('body').innerText()).includes('Floatz'), 'No removed UI branding');
    // New HTML editors must dispatch the original backend contracts rather
    // than opening a native-editor placeholder.
    const nav = async index => {
      await page.locator(`#spine [data-page="${index}"]`).click();
      await page.waitForFunction(() => !document.body.classList.contains('busy'));
    };
    const edit = key => page.locator(`[data-editor-field="${key}"]`);
    const click = async name => { await page.locator(`[data-action="editor:${name}"]`).click(); await page.waitForFunction(() => !document.body.dataset.confirming && !document.body.classList.contains('busy')); };
    const last = method => page.evaluate(name => __calls.filter(c => c.method === name).at(-1), method);
    const labBefore=await page.evaluate(()=>__calls.filter(c=>c.method==='binaryLab').length);
    await nav(13);
    assert.equal(await page.evaluate(()=>__calls.filter(c=>c.method==='binaryLab').length),labBefore,'Binary Lab never scans on page entry');
    await click('lab-inspect');
    assert.ok((await page.locator('#main').innerText()).includes('PE32+'));
    await page.locator('[data-action="editor:lab-tab"][data-tab="exports"]').click();
    assert.equal(await page.locator('.lab-table img').count(),0,'Binary metadata is escaped, not interpreted as HTML');
    await page.locator('[data-action="editor:lab-tab"][data-tab="strings"]').click();
    await edit('lab:query').fill('fixture');await click('lab-strings');
    assert.equal((await last('binaryLab')).args.query,'fixture');
    assert.equal(await page.locator('.lab-table img').count(),0);
    await page.locator('[data-action="editor:lab-tab"][data-tab="sections"]').click();
    await click('lab-peek');
    assert.equal((await last('binaryLab')).args.offset,1024);
    assert.equal((await last('binaryLab')).args.sha256,'a'.repeat(64),'Follow-up native requests bind to inspected hash');
    await page.evaluate(()=>window.__nextFile='C:\\fixture\\report.json');await click('lab-report');
    assert.equal((await last('binaryLab')).args.action,'export');
    assert.equal((await last('binaryLab')).args.kind,'report');
    if(process.env.BSI_WEB_SCREENSHOT)await page.screenshot({path:process.env.BSI_WEB_SCREENSHOT.replace('.png','-binary.png')});
    await nav(6);
    await click('interface-load');
    await edit('remove:0').check();
    await click('removals');
    assert.deepEqual((await last('applyRemovals')).args.names, ['Fixture sidebar']);
    await edit('about:text').fill('Edited About fixture');
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
    await click('maint-sidebar-read');
    await page.locator('[data-action="editor:maint-sidebar-down"]').first().click();
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
    await click('maint-android-scan');
    assert.equal((await last('maintenance')).args.instance,'Nougat32');
    await edit('maint:package:com.fixture.ad').check();
    await click('maint-android-apply');
    maintenanceCall = await last('maintenance');
    assert.deepEqual(maintenanceCall.args.packages,['com.fixture.ad']);
    assert.equal(maintenanceCall.args.keepCopy,true);
    assert.equal(maintenanceCall.args.clearAppData,false);
    await click('maint-disk-find');
    await click('maint-disk-info');
    await edit('maint:name').fill('new-folder');
    const writeCount=await page.evaluate(()=>__calls.filter(c=>c.method==='maintenance'&&c.args.action==='disk-mkdir').length);
    await click('maint-disk-mkdir');
    assert.equal(await page.evaluate(()=>__calls.filter(c=>c.method==='maintenance'&&c.args.action==='disk-mkdir').length),writeCount,'Read-only guard must not dispatch a write');
    await click('maint-disk-enable');
    await click('maint-disk-list');
    assert.equal(await page.locator('.disk-entries button').count(),200,'Bound disk DOM rows');
    await click('maint-disk-next');
    assert.equal(await page.locator('.disk-entries button').count(),40);
    await click('maint-disk-previous');
    await page.locator('[data-action="editor:maint-disk-select"][data-index="1"]').click();
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
    await page.locator('[data-action="editor:gradient-apply"]').click();
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
    await page.locator('#tele [data-action="signOut"]').click();
    await page.locator('#connection').waitFor({ state: 'visible' });
    assert.equal(await page.locator('[data-action]:enabled').count(), 0);
    assert.deepEqual(errors, []);
    console.log('PASS: 14 routes; picker commit/cancel, styled cancellation and reduced-motion window effects; sidebar/debloat/ADB/disk requests; Qt preview controls; offline native actions disabled; manual scans; pagination/search; safe text rendering; real request arguments; failed-engine errors; integrated About/removal/RPC/GIF/boot/gradient/workspace/updater contracts; sign-out lock. UI test transport only.');
  } finally { await browser.close(); }
})().then(() => server.close()).catch(error => { console.error(error); server.close(); process.exitCode = 1; });
