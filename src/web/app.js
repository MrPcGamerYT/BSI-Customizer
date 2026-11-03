/* BSI Customizer engine workspace.
   Native authentication and engines remain the authority; no Python bridge. */
(() => {
  'use strict';
  const $ = s => document.querySelector(s);
  const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  const pages = [
    ['Dashboard', 'Dashboard', 'Customize your emulator.', 'Detailed player patchers, binary inspection and reversible Android maintenance.'],
    ['Color studio', 'Colors', 'Color studio', 'Load the real player palette, stage edits, then apply.'],
    ['Resources', 'Resources', 'Inside your player.', 'Explicit scans only. Inspect, edit and extract embedded resources.'],
    ['Performance', 'FPS', 'More room to perform.', 'The same native FPS detection, analysis and patch engines.'],
    ['Integrity & safety', 'Integrity', 'Inspect before you change.', 'Native integrity checks, patching and restore.'],
    ['Boot experience', 'Boot', 'Make an entrance.', 'Adaptive Qt6/Qt5/WebEngine boot video, timing, cache cleanup and backup recovery.'],
    ['Player interface', 'Interface', 'Less noise. More you.', 'Removals, layout tweaks and editable player About text and links.'],
    ['Discord presence', 'Presence', 'Your player, represented.', 'Keep the existing Rich Presence ID and version-resource utilities.'],
    ['Settings', 'Settings', 'Your workspace.', 'App appearance and target paths. These are not player theme colors.'],
    ['About & updates', 'About', 'Built around your player.', 'Chromium presentation. C++ patch engines. Existing login and update verification.'],
    ['Animated presence', 'Animated', 'Bring presence to life.', 'Configure the existing native GIF RPC plugin and restore flow.'],
    ['Patch workspace', 'Workspace', 'One deliberate change.', 'Portable profiles, queued changes, dry runs and recoverable batch apply.'],
    ['Debloat / Android / Root.vhd', 'Debloat', 'Android & engine maintenance', 'Configuration tuning, live ADB packages, storage cleanup and journalled disk editing.'],
    ['Binary Lab', 'Binary Lab', 'Inside the executable', 'PE sections, imports, exports, Win32 resources, strings and raw bytes. Read-only until you export.']
  ];
  const toolGuides = {
    1:['Theme & gradient editor','Original Auto / QML / Hex methods, presets, individual colors, toggle recoloring, gradients and staged profile support.','Select a target and load its actual palette.','Choose colors or a preset; inspect the dry run.','Apply with the native engine; restore the EXE backup if needed.'],
    2:['Embedded Qt file explorer','Browse code, SVGs, pictures, fonts and binary entries. Text edits and replacements retain native capacity and mapping checks.','Scan Resources explicitly; navigation never scans.','Filter by type, quoted path or min:/max: size.','Inspect, edit, replace or extract a selection; bulk extraction is below.'],
    3:['FPS ceiling patcher','Binary ceiling patching is separate from Android performance tuning. Higher limits do not guarantee actual game FPS.','Detect the QuickTemplates2 DLL for this installation.','Analyze its supported slider exports and set a target limit.','Apply or restore using the original reversible patch engine.'],
    4:['Integrity inspection & recovery','Keep the original inspection, supported-site validation, patch and restore workflow. Unsupported binaries are refused.','Close all emulator instances.','Inspect the selected executable before applying.','Patch only a recognized build; keep the original backup.'],
    5:['Boot video & startup timing','Adaptive Qt6 / Qt5 / WebEngine player, muted loop/crop, boot hold, backup recovery and scoped player-cache cleanup.','Put your supported video at Assets/Promo.mp4.','Set hold duration (0–15000 ms); apply to a recognized player.','Use Workspace to inspect/preview the MP4; emulator codec support can differ.'],
    6:['Player layout, removals & About editor','All original control removals, layout tweaks, sidebar order/visibility, About text and stock links remain here.','Inspect your EXE to discover its supported controls.','Stage only the removals, order and About fields you need.','Apply each operation explicitly; unsupported layouts stay read-only.'],
    7:['Discord ID & Explorer-name patchers','Offline ID discovery, dry-run replacement, undo and Windows version-resource editing. This is separate from the animated RPC plugin.','Scan the actual executable and inspect candidate IDs.','Enter your 17–20 digit application ID and preview.','Apply deliberately, or restore the recorded original.'],
    11:['Profiles, batch plans & media workbench','Original portable profiles, queued patches, hash-bound plans, journals, image preparation and native MP4 preview.','Load or create a profile and queue supported operations.','Build a dry run against the current target hash.','Apply the verified plan; inspect the journal for restoration.']
  };
  const icons = [`<rect x="3" y="3" width="7" height="7" rx="1"/><rect x="14" y="3" width="7" height="7" rx="1"/><rect x="3" y="14" width="7" height="7" rx="1"/><rect x="14" y="14" width="7" height="7" rx="1"/>`, `<circle cx="12" cy="12" r="3"/><path d="M12 3v3M12 18v3M3 12h3M18 12h3M5.6 5.6l2.1 2.1M16.3 16.3l2.1 2.1"/>`, `<rect x="4" y="3" width="16" height="18" rx="2"/><path d="M8 8h8M8 12h8M8 16h4"/>`, `<path d="m4 16 5-7 4 4 7-9M15 4h5v5"/>`, `<path d="m12 3 8 3v6c0 5-8 9-8 9s-8-4-8-9V6zM8 12l3 3 5-6"/>`, `<rect x="3" y="5" width="18" height="14" rx="2"/><path d="m10 9 5 3-5 3z"/>`, `<path d="M5 7h14M5 12h14M5 17h14"/><circle cx="8" cy="7" r="1.5"/><circle cx="15" cy="12" r="1.5"/>`, `<path d="M4 5h16v12H8l-4 4zM8 10h8M8 13h5"/>`, `<circle cx="12" cy="12" r="4"/><path d="M12 3v3M12 18v3M3 12h3M18 12h3M5 5l2 2M17 17l2 2M5 19l2-2M17 7l2-2"/>`, `<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7v1"/>`, `<path d="m12 3 2.3 5.2L20 9l-4 4 .9 5.7-4.9-2.6-4.9 2.6L8 13 4 9l5.7-.8z"/>`, `<rect x="3" y="6" width="18" height="15" rx="2"/><path d="M8 6V3h8v3M3 11h18M10 11v3h4v-3"/>`];
  icons.push(`<path d="M4 6h16M6 6l1 14h10l1-14M9 6V3h6v3M10 10v6M14 10v6"/>`);
  icons.push(`<path d="M4 3h11l5 5v13H4zM14 3v6h6M8 12h2M13 12h3M8 16h2M13 16h3"/>`);
  const svg = i => `<svg viewBox="0 0 24 24" aria-hidden="true">${icons[i]}</svg>`;
  let transport = null, preview = false, state = {}, page = 0, pending = false;
  let palette = { entries: [], hex: [], presets: [], method: 'auto' };
  let kind = 0, measureTimer = null, measureSequence = 0;
  let resources = [], selected = null, resourceData = null, dirty = false, filter = '', offset = 0;
  let operationResult = '', logs = [], editors = null;
  let emulatorIcon='home', emulatorIconColor='#F1F5F9', emulatorIconStroke='1.7', iconStudioOpen=false;
  const drafts = {};
  const explorer=BsiResourceWorkbench.create();
  let resourceFolder='', resourceSort='path', resourceView='list', favoritesOnly=false;
  let originalResourceText='', recentResources=[], searchFrame=0, logFrame=0;
  let operationName='', operationStarted=0, lastDuration=0;
  const htmlCache=new WeakMap();
  const setHTML=(selector,html)=>{const el=$(selector);if(el&&htmlCache.get(el)!==html){el.innerHTML=html;htmlCache.set(el,html);}};
  const resetExplorer=()=>{explorer.reset();resourceFolder='';recentResources=[];originalResourceText='';};

  const enabled = () => !!transport && state.authorized && !state.busy && !pending;
  const panel = body => `<div class="panel"><div class="pscroll">${body}</div></div>`;
  const button = (label, action, primary = false, extra = '') => `<button class="btn${primary ? ' pri' : ''}" data-action="${action}" ${extra} ${enabled() ? '' : 'disabled'}>${label}</button>`;
  const native = () => ''; // No second editor in the normal HTML workflow.
  const confirmDiscard = async () => !dirty || BsiEffects.confirm('Discard the unsaved resource text in this editor?');
  function toast(message) {
    $('#toast').textContent = message; $('#toast').style.display = 'block';
    clearTimeout(toast.timer); toast.timer = setTimeout(() => { $('#toast').style.display = 'none'; }, 5000);
  }
  // Native window pixels are CSS pixels. Layout reflows; never scale text/hit targets.
  addEventListener('beforeunload', event => { if (dirty) { event.preventDefault(); event.returnValue = ''; } });
  function useState(next, preserveResources = false) {
    const pathChanged = next.exePath !== state.exePath;
    const workspaceChanged = next.workspaceRevision !== state.workspaceRevision;
    const updateChanged = JSON.stringify(next.updates) !== JSON.stringify(state.updates);
    const resourcesChanged = next.resourceRevision !== state.resourceRevision;
    state = next;document.body.dataset.nativeFrame=String(state.nativeFrame===true);
    if (pathChanged || !state.authorized) {
      palette = { entries: [], hex: [], presets: [], method: 'auto' };
      resources = []; selected = null; resourceData = null; dirty = false;
      editors?.reset(); resetExplorer();  ++measureSequence; clearTimeout(measureTimer);
    } else if (resourcesChanged && !pending && !preserveResources) {
      resources = [];  selected = null; resourceData = null; dirty = false; resetExplorer();
    }
    if (!state.authorized && !preview) {
      $('#connection').hidden = false;
      $('#connection-message').textContent = 'Sign in through the native Discord login to unlock the workspace.';
      logs = [];
    }
    document.body.dataset.light = String(state.effectiveTheme === 1);
    if (/^#[0-9a-f]{6}$/i.test(state.accent || '')) {
      const rgb = [1, 3, 5].map(i => parseInt(state.accent.slice(i, i + 2), 16));
      document.documentElement.style.setProperty('--a', rgb.join(','));
      document.documentElement.style.setProperty('--on', (rgb[0] * .299 + rgb[1] * .587 + rgb[2] * .114) > 127 ? '#10251b' : '#ffffff');
    }
    chrome();
    if (pathChanged || resourcesChanged && !pending && !preserveResources || !pending && (page === 11 && workspaceChanged || page === 9 && updateChanged)) renderView();
    markBusy();
  }
  function markBusy() {
    document.body.classList.toggle('busy', !!state.busy || pending);transport?.setEditorDirty?.(dirty);
    document.querySelectorAll('[data-window]').forEach(el => {el.disabled = !transport?.windowAction || el.dataset.window==='close' && (!!state.busy || pending);});
    if($('#resource-code'))$('#resource-code').readOnly=pending;
    const status=$('#engine-status');
    status.classList.toggle('working',!!state.busy||pending);
    status.querySelector('span').textContent=pending?operationName.replace(/([A-Z])/g,' $1'):state.busy?'Native engine working':'Engine idle';
    status.querySelector('b').textContent=pending?'Window controls remain available':lastDuration?lastDuration+' ms · last operation':'';
    document.querySelectorAll('[data-action]').forEach(el => {
      el.disabled = !enabled();
    });
  }
  function targetName() {
    const install = (state.installs || []).find(i => i.exe === state.exePath || i.exePath === state.exePath);
    return install?.profile || (state.exeFound ? 'BlueStacks Player' : 'No target selected');
  }
  function chrome() {
    const current = pages[page];
    $('#canvas').classList.toggle('page', page !== 0);
    document.body.dataset.page=String(page);
    setHTML('#spine', [['INSPECT & BUILD',[0,2,13,11]],['PLAYER PATCHERS',[1,3,4,5,6]],['SYSTEM & PRESENCE',[12,7,10]],['APPLICATION',[8,9]]].map(([label,items])=>`<div class="nav-group">${label}</div>${items.map(i=>`<button class="icon${page===i?' on':''}" data-page="${i}" title="${pages[i][0]}" aria-label="${pages[i][0]}" aria-current="${page===i?'page':'false'}">${svg(i)}<span>${pages[i][0]}</span>${i===13?'<em>NEW</em>':''}</button>`).join('')}`).join(''));
    setHTML('#cmd', [2, 13, 12].map(i => `<button class="cmditem${page === i ? ' active' : ''}" data-page="${i}"><strong>${pages[i][1]}</strong></button>`).join(''));
    $('#session').className = 'session' + (state.busy || pending ? ' busy' : !state.exeFound ? ' bad' : '');
    $('#session').innerHTML = `<i></i><b>${esc(preview ? 'Design preview' : state.authorized ? 'Discord verified' : 'Sign-in required')}</b><span>${state.busy || pending ? 'WORKING' : state.exeFound ? 'READY' : 'NO TARGET'}</span><em></em>`;
    setHTML('#hero', `<div class="hero"><div class="hero-main"><div class="kicker">Control center / ${esc(current[1]).toLowerCase()}</div><h1>${esc(current[2])}</h1><p>${esc(current[3])}</p><div class="hero-line"></div></div><div class="target-card"><small>Active target</small><b>${esc(targetName())}</b><div class="ready${state.exeFound ? '' : ' bad'}"><i></i>${state.exeFound ? 'HD-Player.exe detected' : 'Select your player in Settings'}</div></div></div>`);
    setHTML('#profile-options', `<div class="pd-h">Target installations</div>${(state.installs || []).map((install, index) => `<button class="pd-opt" data-action="useInstall" data-index="${index}" ${enabled() ? '' : 'disabled'}><i></i>${esc(install.profile || install.root)}</button>`).join('') || '<p class="note">Detect your installations in Settings.</p>'}<button class="pd-opt" data-page="8">Manage targets ↗</button>`);
    setHTML('#stats', page === 0 ? `<div class="stats">${[['Profile', targetName(), 'Active target'], ['Executable', state.exeFound ? 'Found' : 'Missing', 'HD-Player.exe'], ['Colors', palette.entries.filter(c => !!c.edit).length, 'Staged edits'], ['Resources', resources.length || '—', 'Explicit scans only']].map(([k, v, hint]) => `<div class="stat"><small>${k}</small><b>${esc(v)}</b><span>${hint}</span></div>`).join('')}</div>` : '');
    const meters = [['Colors staged', palette.entries.filter(c => !!c.edit).length, palette.entries.length], ['Resources loaded', resources.length, Math.max(resources.length, 1)], ['Target selected', state.exeFound ? 1 : 0, 1]];
    setHTML('#tele', `<div class="telemetry"><div class="section-label">Live overview</div><h2>System state</h2>${meters.map(([label, value, total], i) => `<div class="meter"><div class="meter-top"><span>${label}</span><b>${i === 2 ? (value ? 'Ready' : 'Not set') : value + ' / ' + total}</b></div><div class="bar ${i === 1 ? 'blue' : i === 2 ? 'white' : ''}"><i style="width:${total ? Math.min(100, value / total * 100) : 0}%"></i></div></div>`).join('')}<div class="divider"></div>${[['Session', preview ? 'Preview only' : state.authorized ? 'Discord verified' : 'Required'], ['Engine', state.busy || pending ? 'Working…' : 'Idle'], ['Version', state.version || '—']].map(([k, v]) => `<div class="mini-status"><span>${k}</span><b>${esc(v)}</b></div>`).join('')}<div class="tele-actions">${button('OPEN PATCH WORKSPACE', 'workspaceHome', true)}${button('RESTORE ORIGINAL', 'restoreBackup')}<div class="tools">${button('Sign out', 'signOut')}</div></div></div>`);
    document.querySelectorAll('[data-window]').forEach(el => { el.disabled = !transport?.windowAction || el.dataset.window==='close' && (!!state.busy || pending); });
    renderLogs(); markBusy();
  }
  function renderLogs() {
    setHTML('#act', `<div class="activity"><div class="activity-head"><span>RECENT ACTIVITY</span><button id="clear-logs">Clear</button></div><div id="logs">${logs.length ? logs.slice(-150).reverse().map(([time, message]) => `<div class="log"><i>${esc(time)}</i>${esc(message)}</div>`).join('') : '<div class="log">No native activity yet.</div>'}</div></div>`);
  }
  let renderedPage=-1;const paneScroll=new Map();
  function renderView() {
    if(renderedPage>=0)paneScroll.set(renderedPage,Object.fromEntries(['.pscroll','.resource-detail','#resource-tree','.resource-list'].map(selector=>[selector,$('#main '+selector)?.scrollTop||0])));
    renderedPage=page;
    let html = '';
    if (page === 0) {
      html = `<div class="matrix">${[[2,'Resource workbench','Explore asset pools, preview vectors and images, inspect metadata and stage source edits.'],[11,'Patch workspace','Build hash-bound plans, review changes and apply with recoverable journals.'],[13,'Binary Lab','Inspect PE sections, imports, exports, strings and raw executable bytes.'],[12,'System & Android','Read system facts, tune frame profiles and manage selected Android instances.']].map(([i, title, desc], n) => `<button class="tile${n === 0 ? ' primary' : ''}" data-page="${i}"><span class="num">0${n + 1}</span><span class="glyph">${svg(i)}</span><h3>${title}</h3><p>${desc}</p></button>`).join('')}</div>`;
    } else if (page === 1) {
      const rows = palette.method === 'hex' ? palette.hex : palette.entries;
      html = panel(`<div class="sec"><div class="sec-h">Live player palette</div><div class="tools"><select class="inp" id="theme-mode"><option value="auto">Auto detect</option><option value="qml">QML (new)</option><option value="hex">Hex (old)</option></select>${button('Load theme', 'loadTheme', true)}${button('Apply staged colors', 'applyTheme')}${native(1)}</div><p class="note">${esc(state.themeStatus || 'Load the target theme to see real colors. No sample values are patched.')}</p><p class="note">${esc(state.themeDetection || '')}${state.themeStale ? ' · Target changed: reload before applying.' : ''}</p>${palette.presets.length ? `<div class="tools"><select class="inp" id="preset">${palette.presets.map(p => `<option value="${esc(p.id)}">${esc(p.name)}</option>`).join('')}</select>${button('Stage preset', 'preset')}</div>` : ''}<div class="color-list">${rows.length ? rows.map((row, index) => {
        const value = row.edit || row.color || row.value || row.current || '#000000';
        const rgb = /^#[0-9a-f]{6}$/i.test(value) ? value : /^#[0-9a-f]{8}$/i.test(value) ? '#' + value.slice(3) : '#000000';
        return `<div class="color-row"><input type="checkbox" data-color-enabled="${index}" aria-label="Enable ${esc(row.name)}" ${(palette.method === 'hex' ? row.enabled : !!row.edit) ? 'checked' : ''} ${enabled() ? '' : 'disabled'}><div><b>${esc(row.name)}</b><small>${esc(row.purpose)}</small></div><input type="color" value="${esc(rgb)}" data-color="${index}" aria-label="Pick ${esc(row.name)}" ${enabled() ? '' : 'disabled'}><input class="inp mono" value="${esc(value)}" data-color="${index}" aria-label="Value for ${esc(row.name)}" ${enabled() ? '' : 'disabled'}></div>`;
      }).join('') : '<div class="empty">No palette loaded. Choose an executable, then click Load theme.</div>'}</div></div>`);
    } else if (page === 2) {
      html = `<div class="resource-workspace"><div class="resource-toolbar"><div class="resource-heading"><span class="eyebrow">ASSET WORKBENCH</span><h2>Resources <span id="resource-total"></span></h2></div>${button('Scan Resources','scanResources',true)}<input class="inp grow" id="resource-filter" placeholder='Search paths · "exact phrase" · min:2kb' value="${esc(filter)}" aria-label="Search resource paths"></div><div class="resource-controls"><select class="inp" id="resource-kind" aria-label="Resource kind">${['All files','Pictures','SVG','Code','Binary','Fonts','Modified vs backup'].map((label,i)=>`<option value="${i}" ${kind===i?'selected':''}>${label}</option>`).join('')}</select><select class="inp" id="resource-sort" aria-label="Sort resources">${[['path','Name · natural'],['size','Size · largest'],['type','File type']].map(([key,label])=>`<option value="${key}" ${resourceSort===key?'selected':''}>${label}</option>`).join('')}</select><div class="view-switch" aria-label="Resource layout"><button class="btn sm" data-resource-view="list" aria-pressed="${resourceView==='list'}">List</button><button class="btn sm" data-resource-view="gallery" aria-pressed="${resourceView==='gallery'}">Gallery</button></div><button class="btn sm" data-resource-local="favorites" aria-pressed="${favoritesOnly}">★ Bookmarks</button><button class="btn sm" data-resource-local="check-page">Select page</button><button class="btn sm" data-resource-local="clear-checks">Clear</button>${button('Export selection…','exportSelection',false,'id="resource-export"')}</div><div class="resource-recent" id="resource-recent"></div><div class="resource-layout"><aside id="resource-tree" aria-label="Resource folders"></aside><section class="resource-browser" aria-label="Scanned resources"><div id="resource-rows"></div></section><section class="resource-detail" aria-label="Resource inspector"><div id="resource-editor"></div><div id="emulator-icon-studio"></div><details class="resource-advanced"><summary>Bulk resource tools</summary>${editors.extra(2)}</details></section></div><footer class="resource-footer"><span>Manual scan only · ${esc(targetName())}</span><span>Qt-native raster previews · guarded writes · Ctrl+S saves draft</span></footer></div>`;
    } else if ([3, 4, 5].includes(page)) {
      const commands = page === 3 ? [['Detect FPS target', 'fpsDetect'], ['Analyze FPS', 'fpsAnalyze'], ['Apply FPS limit', 'fpsPatch'], ['Restore FPS', 'fpsRestore']]
        : page === 4 ? [['Check integrity', 'integrityCheck'], ['Apply integrity patch', 'integrityPatch'], ['Restore integrity', 'integrityRestore']]
        : [['Apply boot patch', 'bootPatch'], ['Purge player cache', 'purgePlayerCache']];
      html = panel(`<div class="sec"><div class="sec-h">${pages[page][0]}</div><p class="native-note">These commands call the existing engines directly. Close all emulator instances before changing files. Backups and validation stay in C++.</p>${page === 3 ? `<label class="lbl" for="fps-limit">FPS limit</label><input id="fps-limit" class="inp" type="number" min="1" max="10000" data-draft="fps" value="${esc(drafts.fps || '240')}">` : page === 5 ? `<label class="lbl" for="boot-hold">Boot hold (milliseconds)</label><input id="boot-hold" class="inp" type="number" min="0" max="15000" data-draft="hold" value="${esc(drafts.hold || '3000')}">` : ''}<div class="button-grid">${commands.map(([label, action]) => button(label, action)).join('')}${native(page)}</div><pre id="operation-result"></pre></div>`);
    } else if (page === 8) {
      html = panel(`<div class="sec"><div class="sec-h">Application appearance</div><div class="tools"><select class="inp" id="appearance-mode" aria-label="App theme">${['Dark', 'Light', 'System'].map((v, i) => `<option value="${i}" ${state.appearance === i ? 'selected' : ''}>${v}</option>`).join('')}</select><input type="color" id="accent" aria-label="App accent" value="${esc(state.accent || '#9ce9c1')}">${button('Save appearance', 'appearance', true)}</div></div><div class="sec"><div class="sec-h">Native targets</div>${['exe', 'dll', 'conf'].map(role => `<label class="lbl" for="path-${role}">${{ exe: 'HD-Player.exe', dll: 'Qt5/Qt6 QuickTemplates2 DLL', conf: 'bluestacks.conf' }[role]}</label><div class="path-row"><input class="inp mono" id="path-${role}" value="${esc(state[role + 'Path'] || '')}">${button('Browse…', 'browse', false, `data-role="${role}"`)}${button('Set path', 'path', false, `data-role="${role}"`)}</div>`).join('')}<div class="button-grid">${button('Detect installations', 'detectInstalls')}${button('Restore EXE backup', 'restoreBackup')}</div>${(state.installs || []).map((install, index) => `<div class="kv"><span>${esc(install.profile || install.root)}</span>${button('Use installation', 'useInstall', false, `data-index="${index}"`)}</div>`).join('')}</div>`);
    } else {
      html = panel(editors.render(page));
    }
    $('#main').innerHTML = html;
    const guide=page===2?null:toolGuides[page];
    if(guide) $('#main .pscroll')?.insertAdjacentHTML('afterbegin',`<div class="tool-workflow"><div><span class="eyebrow">ENGINE WORKFLOW</span><h3>${esc(guide[0])}</h3><p>${esc(guide[1])}</p></div><ol>${guide.slice(2).map((step,i)=>`<li><b>0${i+1}</b><span>${esc(step)}</span></li>`).join('')}</ol></div>`);
    if (page !== 0) $('#main .pscroll')?.insertAdjacentHTML('beforeend', editors.extra(page) + (page === 8 ? editors.extra(0) : ''));
    if (page === 2) { renderResources(); renderResourceEditor(); }
    if ($('#operation-result')) $('#operation-result').textContent = operationResult;
    for(const [selector,top]of Object.entries(paneScroll.get(page)||{})){const el=$('#main '+selector);if(el)el.scrollTop=top;}
    markBusy(); syncVideo();
  }
  const rasterPreview = value => /^data:image\/(png|jpeg|webp|gif|bmp);base64,/i.test(value||'');
  let resourcePreviewBackground='checker';
  function visibleResources(){return explorer.query({text:filter,kind,folder:resourceFolder,sort:resourceSort,favorites:favoritesOnly});}
  let lastResourceViewKey='';
  function renderResources() {
    if (!$('#resource-rows')) return;
    const viewKey=JSON.stringify([filter,kind,resourceFolder,resourceSort,resourceView,favoritesOnly,offset]);
    const oldTop=viewKey===lastResourceViewKey?$('.resource-list')?.scrollTop||0:0;lastResourceViewKey=viewKey;
    const treeTop=$('#resource-tree')?.scrollTop||0;
    const rows=visibleResources();
    offset=Math.max(0,Math.min(offset,Math.max(0,Math.ceil(rows.length/100)-1)*100));
    $('#resource-total').textContent=resources.length?resources.length.toLocaleString():'';
    const children=explorer.children(resourceFolder), parent=resourceFolder.split('/').slice(0,-1).join('/');
    $('#resource-tree').innerHTML=`<div class="tree-label">RESOURCE POOLS</div><button class="folder-row ${!resourceFolder?'on':''}" data-resource-folder="">▦ All resources <small>${resources.length}</small></button>${resourceFolder?`<button class="folder-row" data-resource-folder="${esc(parent)}">↰ Parent folder</button><p class="tree-current">${esc(resourceFolder)}</p>`:''}${children.slice(0,200).map(folder=>`<button class="folder-row" data-resource-folder="${esc(folder.path)}" title="${esc(folder.path)}"><span>▱ ${esc(folder.name)}</span><small>${folder.count}</small></button>`).join('')}${children.length>200?'<p class="note">First 200 folders. Use path search from All resources for others.</p>':''}<div class="tree-hint">Local index only<br>Search never scans your EXE.<br><br>Browse · inspect · stage · validate · save</div>`;
    $('#resource-recent').innerHTML=recentResources.length?recentResources.map(path=>`<button class="recent-tab${path===selected?.path?' active':''}" data-resource-recent="${esc(path)}" title="${esc(path)}">${esc(path.split('/').pop())}${path===selected?.path&&dirty?' •':''}</button>`).join(''):'<span>Open a resource to start an inspection session. No files loaded automatically.</span>';
    $('#resource-rows').innerHTML=`<div class="resource-result-bar"><b>${rows.length.toLocaleString()} matches</b><span>${rows.length?offset+1:0}–${Math.min(offset+100,rows.length)} · ${explorer.checked.size} selected</span></div><div class="resource-list ${resourceView==='gallery'?'resource-gallery':''}" role="group" aria-label="Resource results">${rows.slice(offset,offset+100).map(r=>`<div class="resource-item"><label class="resource-check"><input type="checkbox" data-resource-check="${r.index}" aria-label="Select ${esc(r.path)} for export" ${explorer.checked.has(r.path)?'checked':''}></label><button class="resource-row${r.index===selected?.index?' selected':''}" data-resource="${r.index}" ${enabled()?'':'disabled'} title="${esc(r.path)}"><span class="resource-thumb">${rasterPreview(r.thumbnail)?`<img src="${esc(r.thumbnail)}" loading="lazy" alt="${esc(r.name||r.path)} preview">`:`<b>${esc(r.format||r.kind)}</b>`}</span><span class="resource-row-info"><b>${esc(r.path.split('/').pop())}</b><small>${esc(r.format||r.kind)} · ${Number(r.size).toLocaleString()} B${r.modified?' · MODIFIED':''}${explorer.bookmarks.has(r.path)?' · ★':''}</small><span class="resource-row-path">${esc(r.path)}</span></span></button></div>`).join('')||`<div class="resource-empty"><span>▦</span><h3>${resources.length?'No matching resources':'Explore what’s inside.'}</h3><p>${resources.length?'Adjust your folder, type or search filter.':'Select an emulator executable in Settings, then Scan Resources. Code, vectors, images, fonts and binary records appear here.'}</p></div>`}</div><div class="resource-pagination"><button class="btn sm" data-paginate="-1" ${offset?'':'disabled'}>Previous</button><span>${rows.length?Math.floor(offset/100)+1:0} / ${Math.ceil(rows.length/100)}</span><button class="btn sm" data-paginate="1" ${offset+100<rows.length?'':'disabled'}>Next</button></div>`;
    $('.resource-list').scrollTop=oldTop;$('#resource-tree').scrollTop=treeTop;
    const exp=$('#resource-export');if(exp)exp.textContent=`Export ${explorer.checked.size||'selection'}…`;
  }
  async function resourceInspection(index){
    const candidate=resources.find(row=>row.index===index);
    if(!candidate)throw Error('This resource is no longer in the scan. Scan Resources again.');
    return Number(candidate.size)>8*1024*1024?{metadataOnly:true,text:'',preview:'',hex:'',note:'Metadata-only inspection: inline previews are limited to 8 MiB. Use Extract to inspect this resource externally.',budget:{known:false,msg:'Inline inspection skipped; original bytes remain unchanged.'}}:await request('readResource',{index});
  }
  async function selectResource(index){
    if(!enabled()||!await confirmDiscard())return;
    const data=await resourceInspection(index);
    ++measureSequence;clearTimeout(measureTimer);
    selected=resources.find(r=>r.index===index);if(!selected)return;
    resourceData=data;originalResourceText=data.text||'';dirty=false;
    recentResources=[...recentResources.filter(path=>path!==selected.path),selected.path].slice(-8);
    renderResources();renderResourceEditor();
  }
  function renderEmulatorIcons() {
    const box=$('#emulator-icon-studio'); if(!box)return;
    box.innerHTML=BsiEmulatorIcons.render({esc,button,selected:resourceData?.metadataOnly?null:selected,preview:resourceData?.preview,choice:emulatorIcon,color:emulatorIconColor,stroke:emulatorIconStroke});
    $('#emu-icon-details').open=iconStudioOpen;
    markBusy();
  }
  function renderResourceEditor() {
    transport?.setEditorDirty?.(dirty);
    renderEmulatorIcons();
    if (!$('#resource-editor')) return;
    const box = $('#resource-editor');
    if (!selected || !resourceData) { box.innerHTML = '<div class="resource-empty inspector-empty"><span>⌕</span><h3>Inspector</h3><p>Choose an asset to preview its saved content, inspect storage metadata, edit source or export.</p><small>Nothing is executed as code.</small></div>'; return; }
    const picture=rasterPreview(resourceData.preview),canEdit=selected.text&&!resourceData.metadataOnly;
    box.innerHTML = `<div class="inspector-title"><span class="eyebrow">${esc(selected.format||selected.kind)} / INSPECTOR</span><span id="draft-indicator">${dirty?'Unsaved draft':'Saved resource'}</span></div><h3 class="inspector-name">${esc(selected.path.split('/').pop())}</h3><p class="inspector-path">${esc(selected.path)}</p><div class="tools"><button class="btn sm" data-resource-local="bookmark">${explorer.bookmarks.has(selected.path)?'★ Bookmarked':'☆ Bookmark'}</button><button class="btn sm" data-resource-local="copy-path">Copy path</button>${canEdit?'<button class="btn sm" data-resource-local="revert-draft">Reset draft</button>':''}</div><details class="resource-metadata"><summary>Storage & mapping metadata</summary><dl>${[['Path',selected.path],['Format',selected.format||selected.kind],['Decoded bytes',selected.size],['Stored bytes',selected.stored],['Compression',selected.compressed?'Compressed':'Uncompressed'],['EXE record offset',Number.isFinite(selected.entryOff)?'0x'+selected.entryOff.toString(16):'Unavailable'],['Dimensions',selected.width?selected.width+' × '+selected.height:'Not applicable'],['Modified vs backup',selected.modifiedKnown?(selected.modified?'Yes':'No'):'Not compared']].map(([key,value])=>`<dt>${esc(key)}</dt><dd>${esc(value)}</dd>`).join('')}</dl></details><p class="note" id="resource-budget"></p><div class="resource-inspector${picture?' has-picture':''}">${picture?`<div><div class="tools"><label class="note">Preview background <select class="inp" id="resource-preview-bg">${['checker','dark','light'].map(bg=>`<option value="${bg}" ${bg===resourcePreviewBackground?'selected':''}>${bg}</option>`).join('')}</select></label><button class="btn sm" data-resource-zoom="fit">Fit</button><button class="btn sm" data-resource-zoom="100">100%</button><button class="btn sm" data-resource-zoom="200">200%</button></div><div class="resource-picture" id="resource-picture" data-background="${resourcePreviewBackground}"><img id="resource-image" alt="Saved SVG / image rendered by the native Qt decoder"></div><p class="note">Saved/native raster preview (static frame for animated formats). SVG source is not injected into the page. Unsaved drafts are not rendered here.</p></div>`:''}<div>${canEdit?'<div class="source-toolbar"><label class="lbl" for="resource-code">Source editor</label><input class="inp" id="resource-find" placeholder="Find exact text…" aria-label="Find in resource source"><button class="btn sm" data-resource-local="find-next">Next</button></div><textarea class="inp" id="resource-code" maxlength="8388608" spellcheck="false" aria-label="Resource source text"></textarea>':`<p class="note">${esc(resourceData.note||'No supported image preview. Inspect bytes or extract the file.')}</p>`}<details class="resource-byte-details"><summary>Raw bytes · first 256 decoded bytes</summary><pre class="lab-hex" id="resource-hex"></pre><p class="note">Offsets are within the decoded resource, not EXE file offsets. Fonts/compiled data are not executed or loaded as code.</p></details></div></div><div class="button-grid">${canEdit?button('Save resource text','saveResource',true):''}${button('Replace with file…','replaceResource')}${button('Extract…','extractResource')}</div>`;
    const budget=resourceData.budget||{};
    $('#resource-budget').textContent=`${selected.format||selected.kind} · Decoded: ${selected.size} B · Stored: ${selected.stored} B · Capacity: ${budget.known?budget.capacity+' B':'unknown'} · ${budget.msg||'Validated by native engine on save.'}`;
    if($('#resource-code'))$('#resource-code').value=resourceData.text||'';
    if($('#resource-image'))$('#resource-image').src=resourceData.preview;
    $('#resource-hex').textContent=resourceData.hex||'No byte sample available. Extract the resource to inspect it externally.';
  }
  function scheduleMeasurement() {
    const sequence = ++measureSequence;
    clearTimeout(measureTimer);
    if (!selected || !resourceData) return;
    if($('#resource-budget'))$('#resource-budget').textContent='Draft changed · waiting for native payload measurement. No file has been written.';
    const index = selected.index, text = resourceData.text, path = state.exePath;
    measureTimer = setTimeout(async () => {
      if(sequence!==measureSequence)return;
      if(!enabled()){scheduleMeasurement();return;}
      try {
        // Native measurement uses cached compression metadata; never scans or saves.
        const size = await transport.invoke('measureResource', { index, text, bom: !!resourceData?.budget?.bom });
        if (sequence !== measureSequence || selected?.index !== index || state.exePath !== path || !$('#resource-budget')) return;
        const capacity = resourceData.budget?.capacity;
        $('#resource-budget').textContent = `Edited decoded: ${size.decoded} B · Encoded payload: ${size.encoded} B · Capacity: ${capacity ?? 'unknown'} B · ${size.encoded > capacity ? 'Needs verified relocation; native save may refuse.' : 'Native validation still required on save.'}`;
      } catch (_) {if(sequence===measureSequence&&$('#resource-budget'))$('#resource-budget').textContent='Measurement unavailable; native Save still validates the actual payload. No estimate is assumed.';}
    }, 300);
  }
  async function request(method, args = {}) {
    if (!enabled()) throw new Error('Native operations require the desktop runtime, a verified Discord session and an idle engine.');
    pending = true; operationName=method;operationStarted=performance.now();markBusy();
    await new Promise(resolve=>{const fallback=setTimeout(resolve,40);requestAnimationFrame(()=>{clearTimeout(fallback);setTimeout(resolve,0);});});
    try {
      const value = await transport.invoke(method, args);
      // Do not stringify multi-megabyte images/source/thumbnail arrays into a hidden result box.
      if(!['listResources','readResource','readTheme','readWorkspace'].includes(method))operationResult=typeof value==='string'?value:JSON.stringify(value??'Native request completed.',null,2);
      lastDuration=Math.round(performance.now()-operationStarted);
      if (value?.msg) toast(value.msg);
      return value;
    } finally {
      let latest;
      try { latest = await transport.snapshot(); } catch (_) { /* native process may be closing */ }
      pending = false;
      if (latest) useState(latest, true);
      chrome();
    }
  }
  async function readPalette() { palette = await request('readTheme'); }
  async function refreshResources() {
    const list = await request('listResources');
    resources = list.map((r, i) => ({ ...r, index: i }));
    explorer.setRows(resources);
    if(selected){const path=selected.path;selected=resources.find(row=>row.index===selected.index&&row.path===path)||resources.find(row=>row.path===path)||null;if(!selected){resourceData=null;dirty=false;}}
  }
  async function navigate(index) {
    if (!Number.isInteger(index) || index < 0 || index >= pages.length || index === page) return;
    const viewingWhileBusy=pending||state.busy;
    if(viewingWhileBusy&&dirty){toast('Let the current operation finish before leaving this unsaved resource draft.');return;}
    if(!viewingWhileBusy&&!await confirmDiscard())return;
    if(!viewingWhileBusy&&dirty){selected=null;resourceData=null;dirty=false;}
    $('#profile-menu').open = false;
    page = index; operationResult = ''; chrome(); renderView(); BsiEffects.enter($('#main'));
    // Reads only the already-loaded palette. Neither this nor Resources navigation scans files.
    if (page === 1 && enabled()) { await readPalette(); renderView(); }
    await editors.navigate(page);
    if ([5, 10, 11].includes(page)) renderView();
  }
  async function action(el) {
    const name = el.dataset.action;
    if (!enabled()) return;
    if (name.startsWith('editor:')) { await editors.action(name.slice(7), el, page); renderView(); return; }
    if (name === 'stageEmulatorIcon') {
      if(!BsiEmulatorIcons.eligible(selected)||!resourceData||resourceData.metadataOnly)throw Error('Select a scanned, text-editable SVG button icon first.');
      const icon=BsiEmulatorIcons.icons.find(i=>i.id===emulatorIcon);
      if(!await BsiEffects.confirm(`Stage ${icon.label} for ${selected.path}? This replaces the editor draft only. Button behavior stays unchanged. Use Save resource text separately to write.`))return;
      // Parsing XML in an inert document is not rendering arbitrary SVG markup.
      // Only numeric root dimensions are carried into the trusted icon template.
      const root=new DOMParser().parseFromString(resourceData.text||'', 'image/svg+xml').documentElement;
      const dimension=key=>root?.localName==='svg'&&/^(?:\d+(?:\.\d+)?)(?:px)?$/.test(root.getAttribute(key)||'')?parseFloat(root.getAttribute(key)):24;
      resourceData.text=BsiEmulatorIcons.svg(emulatorIcon,{color:emulatorIconColor,stroke:emulatorIconStroke,width:dimension('width'),height:dimension('height')});
      dirty=true; renderResourceEditor(); scheduleMeasurement();
      toast('Icon staged only. Review the draft and native budget, then Save resource text to apply.');return;
    }
    if (name === 'native') { if (await confirmDiscard()) { if (dirty) { selected = null; resourceData = null; } dirty = false; await request('openNativeTool', { page: Number(el.dataset.page) }); } return; }
    if (name === 'loadTheme') { await request('loadTheme', { mode: $('#theme-mode').value }); await readPalette(); }
    else if (name === 'applyTheme') { if (!await BsiEffects.confirm('Apply staged colors to the selected EXE using the native engine?')) return; await request(name); await readPalette(); }
    else if (name === 'preset') { await request('applyPreset', { id: $('#preset').value }); await readPalette(); }
    else if (name === 'exportSelection') {const indices=resources.filter(row=>explorer.checked.has(row.path)).map(row=>row.index);if(!indices.length)throw Error('Select resources using their checkboxes first.');const directory=await transport.chooseFile('resources-folder');if(!directory)return;if(!await BsiEffects.confirm(`Export ${indices.length} resources to a NEW subfolder? Source EXE remains unchanged. Limits: 500 entries, 16 MiB per entry, 128 MiB total. Filenames receive their native index to prevent collisions.`))return;await request('exportResourceSelection',{indices,directory});}
    else if (name === 'scanResources') { if (!await confirmDiscard()) return; dirty = false; selected = null; resourceData = null; recentResources=[];resourceFolder='';await request(name); await refreshResources(); }
    else if (name === 'saveResource') { if (!await BsiEffects.confirm('Save this resource into the selected EXE? The native engine will validate storage capacity.')) return; await request(name, { index: selected.index, text: resourceData.text }); dirty = false; await refreshResources(); resourceData = selected?await resourceInspection(selected.index):null;originalResourceText=resourceData?.text||''; }
    else if (name === 'replaceResource' || name === 'extractResource') {
      if (name === 'replaceResource' && !await confirmDiscard()) return;
      const path = await transport.chooseFile(name === 'replaceResource' ? 'replace' : 'extract');
      if (!path) return;
      if (name === 'replaceResource' && !await BsiEffects.confirm('Replace the selected EXE resource with this file?')) return;
      await request(name, { index: selected.index, path });
      if (name === 'replaceResource') { dirty = false; await refreshResources(); resourceData = selected?await resourceInspection(selected.index):null;originalResourceText=resourceData?.text||''; }
    } else if (name === 'browse') { if (!await confirmDiscard()) return; const path = await transport.chooseFile(el.dataset.role); if (!path) return; }
    else if (name === 'path') { if (!await confirmDiscard()) return; const role = el.dataset.role; await request('setPaths', { [role]: $('#path-' + role).value }); }
    else if (name === 'appearance') await request('setAppearance', { theme: Number($('#appearance-mode').value), accent: $('#accent').value });
    else if (name === 'detectInstalls') await request(name);
    else if (name === 'useInstall') { if (!await confirmDiscard()) return; await request(name, { index: Number(el.dataset.index) }); }
    else if (name === 'signOut') { if (await confirmDiscard()) await request(name); return; }
    else {
      const mutating = ['fpsPatch', 'fpsRestore', 'integrityPatch', 'integrityRestore', 'bootPatch', 'purgePlayerCache', 'restoreBackup'].includes(name);
      if (mutating && !await BsiEffects.confirm('Run ' + name + ' on the selected target? Close all emulator instances first.')) return;
      await request(name, name === 'fpsPatch' ? { limit: Number($('#fps-limit').value) } : name === 'bootPatch' ? { holdMs: Number($('#boot-hold').value) } : {});
    }
    renderView();
  }
  document.addEventListener('click', async event => {
    try {
      const zoom=event.target.closest('[data-resource-zoom]');
      if(zoom){const img=$('#resource-image');if(img){const n=zoom.dataset.resourceZoom;img.style.maxWidth=n==='fit'?'100%':'none';img.style.maxHeight=n==='fit'?'100%':'none';img.style.width=n==='fit'?'auto':img.naturalWidth*(Number(n)/100)+'px';}return;}
      const iconChoice=event.target.closest('[data-emulator-icon]');
      if(iconChoice){if(BsiEmulatorIcons.icons.some(i=>i.id===iconChoice.dataset.emulatorIcon)){emulatorIcon=iconChoice.dataset.emulatorIcon;renderEmulatorIcons();}return;}
      const windowButton = event.target.closest('[data-window]');
      if (windowButton) { if (windowButton.dataset.window === 'close' && !await confirmDiscard()) return; if(windowButton.dataset.window!=='close'||!state.busy&&!pending) await BsiEffects.windowAction(windowButton.dataset.window, action => transport?.windowAction?.(action)); return; }
      const actionEl = event.target.closest('[data-action]');
      if (actionEl) { if (actionEl.dataset.action === 'workspaceHome') await navigate(11); else await action(actionEl); return; }
      const nav = event.target.closest('button[data-page]');
      if (nav) { await navigate(Number(nav.dataset.page)); return; }
      const local=event.target.closest('[data-resource-local]');
      if(local){
        const command=local.dataset.resourceLocal;
        if(command==='favorites'){favoritesOnly=!favoritesOnly;local.setAttribute('aria-pressed',favoritesOnly);offset=0;renderResources();}
        if(command==='check-page'){for(const row of visibleResources().slice(offset,offset+100))explorer.checked.add(row.path);renderResources();}
        if(command==='clear-checks'){explorer.checked.clear();renderResources();}
        if(command==='bookmark'&&selected){explorer.bookmark(selected.path);renderResources();renderResourceEditor();}
        if(command==='revert-draft'&&resourceData&&await confirmDiscard()){resourceData.text=originalResourceText;dirty=false;renderResourceEditor();renderResources();scheduleMeasurement();}
        if(command==='copy-path'&&selected){const copy=document.createElement('textarea');copy.value=selected.path;document.body.append(copy);copy.select();const ok=document.execCommand('copy');copy.remove();toast(ok?'Resource path copied.':'Clipboard unavailable; select the path in metadata and copy it.');}
        if(command==='find-next'){const code=$('#resource-code'),query=$('#resource-find')?.value;if(code&&query){let at=code.value.indexOf(query,code.selectionEnd);if(at<0)at=code.value.indexOf(query);if(at<0)toast('No exact match in source.');else{code.focus();code.setSelectionRange(at,at+query.length);const line=code.value.slice(0,at).split('\n').length;code.scrollTop=Math.max(0,(line-4)*20);}}}
        return;
      }
      const folder=event.target.closest('[data-resource-folder]');if(folder){resourceFolder=folder.dataset.resourceFolder;offset=0;renderResources();return;}
      const view=event.target.closest('[data-resource-view]');if(view){resourceView=view.dataset.resourceView;document.querySelectorAll('[data-resource-view]').forEach(el=>el.setAttribute('aria-pressed',el.dataset.resourceView===resourceView));renderResources();return;}
      const recent=event.target.closest('[data-resource-recent]');if(recent){const row=resources.find(r=>r.path===recent.dataset.resourceRecent);if(row)await selectResource(row.index);return;}
      const row=event.target.closest('[data-resource]');if(row){await selectResource(Number(row.dataset.resource));return;}
      const paginate = event.target.closest('[data-paginate]');
      if (paginate) { offset += Number(paginate.dataset.paginate) * 100; renderResources(); }
      if (event.target.closest('#clear-logs')) { logs = []; renderLogs(); }
    } catch (error) { toast(error.message || String(error)); renderView(); }
  });
  document.addEventListener('toggle',event=>{if(event.target.id==='emu-icon-details'&&event.target.isConnected)iconStudioOpen=event.target.open;},true);
  $('.title').addEventListener('pointerdown', event => { if (event.button === 0) transport?.windowAction?.('move'); });
  $('.title').addEventListener('dblclick', () => transport?.windowAction?.('max'));
  document.addEventListener('input', event => {
    const el = event.target;
    editors.input(el);
    if(el.id==='emu-icon-color'&&/^#[0-9a-f]{6}$/i.test(el.value)){emulatorIconColor=el.value;renderEmulatorIcons();return;}
    if (el.id === 'resource-filter') { filter = el.value; offset = 0; clearTimeout(searchFrame);searchFrame=setTimeout(renderResources,70); }
    if (el.id === 'resource-code' && resourceData) { resourceData.text = el.value; dirty = true;transport?.setEditorDirty?.(true); if($('#draft-indicator'))$('#draft-indicator').textContent='Unsaved draft';scheduleMeasurement(); }
    if (el.dataset.draft) drafts[el.dataset.draft] = el.value;
  });
  document.addEventListener('change', async event => {
    const el = event.target;
    editors.input(el);
    if(el.id==='resource-preview-bg'){resourcePreviewBackground=el.value;$('#resource-picture')?.setAttribute('data-background',el.value);return;}
    if(el.id==='emu-icon-stroke'&&['1.4','1.7','2'].includes(el.value)){emulatorIconStroke=el.value;renderEmulatorIcons();return;}
    if(el.dataset.resourceCheck!==undefined){const row=resources.find(row=>row.index===Number(el.dataset.resourceCheck));if(row)explorer.check(row.path);renderResources();return;}
    if(el.id==='resource-sort'){resourceSort=el.value;offset=0;renderResources();return;}
    if (el.id === 'resource-kind') { kind = Number(el.value); offset = 0; renderResources(); return; }
    const index = el.dataset.color ?? el.dataset.colorEnabled;
    if (index === undefined || !enabled()) return;
    const rows = palette.method === 'hex' ? palette.hex : palette.entries;
    const row = rows[Number(index)]; if (!row) return;
    const previous = row.edit || row.color || row.value || row.current;
    const value = el.dataset.color !== undefined ? (el.type === 'color' && /^#[0-9a-f]{8}$/i.test(previous) ? previous.slice(0, 3) + el.value.slice(1) : el.value) : previous;
    if (!/^#[0-9a-f]{6}([0-9a-f]{2})?$/i.test(value)) { toast('Use #RRGGBB or #AARRGGBB.'); renderView(); return; }
    try {
      await request('setThemeEdit', { name: row.name, value, enabled: el.dataset.colorEnabled !== undefined ? el.checked : true });
      await readPalette(); renderView();
    } catch (error) { toast(error.message); }
  });
  $('#preview-button').onclick = () => {
    preview = true; state = {}; $('#connection').hidden = true; $('#preview-note').hidden = false;
    chrome(); renderView();
  };
  let lowEffects=false;try{lowEffects=localStorage.getItem('bsi.presentation.lowEffects')==='true';}catch(_){}
  function applyEffects(){document.body.dataset.effects=lowEffects?'low':'full';$('#effects-toggle').setAttribute('aria-pressed',String(lowEffects));}
  applyEffects();$('#effects-toggle').onclick=()=>{lowEffects=!lowEffects;applyEffects();try{localStorage.setItem('bsi.presentation.lowEffects',String(lowEffects));}catch(_){}};
  function commandResults(){const needle=$('#command-search').value.toLowerCase();$('#command-results').innerHTML=pages.map((entry,i)=>({entry,i})).filter(({entry})=>entry.join(' ').toLowerCase().includes(needle)).map(({entry,i})=>`<button class="command-result" data-command-page="${i}">${svg(i)}<span><b>${esc(entry[0])}</b><small>${esc(entry[3])}</small></span><em>↵</em></button>`).join('')||'<p class="empty">No tools match. Try Resources, FPS, ADB or Binary.</p>';}
  function openCommands(){commandResults();$('#command-dialog').showModal();$('#command-search').focus();}
  $('#command-open').onclick=openCommands;$('#command-close').onclick=()=>$('#command-dialog').close();$('#command-search').oninput=commandResults;
  $('#command-results').onclick=async event=>{const el=event.target.closest('[data-command-page]');if(!el)return;$('#command-dialog').close();await navigate(Number(el.dataset.commandPage));};
  $('#command-dialog').addEventListener('keydown',event=>{if(event.key==='ArrowDown'||event.key==='ArrowUp'){event.preventDefault();const items=[...document.querySelectorAll('.command-result')];let at=items.indexOf(document.activeElement);items[(at+(event.key==='ArrowDown'?1:-1)+items.length)%items.length]?.focus();}if(event.key==='Enter'&&document.activeElement.id==='command-search'){event.preventDefault();$('.command-result')?.click();}});
  document.addEventListener('keydown',async event=>{
    if((event.ctrlKey||event.metaKey)&&event.key.toLowerCase()==='k'){event.preventDefault();if(!$('#command-dialog').open)openCommands();return;}
    if((event.ctrlKey||event.metaKey)&&event.key.toLowerCase()==='s'&&page===2){event.preventDefault();if(dirty&&enabled())await action($('[data-action="saveResource"]'));return;}
    if(event.key==='Tab'&&event.target.id==='resource-code'&&!event.target.readOnly){event.preventDefault();event.target.setRangeText('  ',event.target.selectionStart,event.target.selectionEnd,'end');event.target.dispatchEvent(new Event('input',{bubbles:true}));}
  });
  let videoFrame=0, videoGeometry='';
  function syncVideo() {
    cancelAnimationFrame(videoFrame);
    videoFrame=requestAnimationFrame(() => {
      if(!transport || !state.authorized) {videoGeometry='';return;}
      const host=$('#video-host'), rect=host?.getBoundingClientRect(), pane=$('#main')?.getBoundingClientRect();
      const animating=$('#main')?.getAnimations().some(a=>a.playState==='running');
      // Native pixels must obey the HTML scroller's clipping, not cover its
      // header/footer when the preview scrolls partly out of view.
      const clipX=Math.max(rect?.left||0,pane?.left||0,0), clipY=Math.max(rect?.top||0,pane?.top||0,0);
      const clipWidth=Math.max(0,Math.min(rect?.right||0,pane?.right||0,innerWidth)-clipX);
      const clipHeight=Math.max(0,Math.min(rect?.bottom||0,pane?.bottom||0,innerHeight)-clipY);
      const visible=!!rect && clipWidth>0 && clipHeight>0 && !document.querySelector('dialog[open]') && !animating && !document.body.classList.contains('minimizing') && !document.body.classList.contains('closing');
      const geometry={action:'geometry',visible,x:rect?.x||0,y:rect?.y||0,width:rect?.width||0,height:rect?.height||0,clipX,clipY,clipWidth,clipHeight};
      const key=JSON.stringify(geometry);
      if(key!==videoGeometry) {videoGeometry=key; transport.invoke('videoControl',geometry).catch(()=>{videoGeometry='';});}
      if(animating) syncVideo();
    });
  }
  addEventListener('resize',syncVideo);
  document.addEventListener('scroll',syncVideo,true);
  new MutationObserver(syncVideo).observe(document.body,{childList:true,subtree:true,attributes:true,attributeFilter:['open','class']});
  async function start() {
    chrome(); renderView();
    try {
      transport = await BsiTransport.connect();
      transport.onState(useState);
      transport.onVideo?.(message => editors.videoStatus(message));
      transport.onLog(message => { logs.push([new Date().toLocaleTimeString(), message]); if (logs.length > 500) logs.shift(); if(!logFrame)logFrame=requestAnimationFrame(()=>{logFrame=0;renderLogs();}); });
      const initial = await transport.snapshot();
      if (initial.protocol !== 1) throw new Error('Unsupported native bridge protocol. Restart with --native-ui.');
      useState(initial);
      if (state.authorized) $('#connection').hidden = true;
      chrome(); renderView();
    } catch (error) {
      transport = null; $('#connection-message').textContent = error.message;
      $('#preview-button').hidden = false;
    }
  }
  editors = BsiEditors.create({ esc, button, request, enabled,
    chooseFile: role => transport.chooseFile(role), getState: () => state,
    getResources: () => resources, refreshPalette: readPalette,
    invalidateResources: () => { resetExplorer(); resources = [];  selected = null; resourceData = null; dirty = false; ++measureSequence; clearTimeout(measureTimer); },
    setBootHold: hold => { drafts.hold = String(hold); }
  });
  start();
})();
