/* BSI Chromium workspace. Visual concept: @Floatz4Ever.
   Native authentication and engines remain the authority; no Python bridge. */
(() => {
  'use strict';
  const $ = s => document.querySelector(s);
  const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  const pages = [
    ['Overview', 'Dashboard', 'Customize your emulator.', 'Precision tools. Native C++ engines. Your existing Discord session.'],
    ['Theme studio', 'Colors', 'Color studio', 'Load the real player palette, stage edits, then apply.'],
    ['Resource explorer', 'Resources', 'Inside your player.', 'Explicit scans only. Inspect, edit and extract embedded resources.'],
    ['Performance', 'FPS', 'More room to perform.', 'The same native FPS detection, analysis and patch engines.'],
    ['Integrity & safety', 'Integrity', 'Inspect before you change.', 'Native integrity checks, patching and restore.'],
    ['Boot experience', 'Boot', 'Make an entrance.', 'Configure the native boot patch or open the complete editor.'],
    ['Player interface', 'Interface', 'Less noise. More you.', 'Removals, layout tweaks and editable player About text and links.'],
    ['Discord presence', 'Presence', 'Your player, represented.', 'Keep the existing Rich Presence ID and version-resource utilities.'],
    ['Appearance', 'Settings', 'Your workspace.', 'App appearance and target paths. These are not player theme colors.'],
    ['About BSI', 'About', 'Built around your player.', 'Chromium presentation. C++ patch engines. Existing login and update verification.'],
    ['Animated presence', 'Animated', 'Bring presence to life.', 'Configure the existing native GIF RPC plugin and restore flow.'],
    ['Patch workspace', 'Workspace', 'One deliberate change.', 'Portable profiles, queued changes, dry runs and recoverable batch apply.']
  ];
  let transport = null, preview = false, state = {}, page = 0, pending = false;
  let palette = { entries: [], hex: [], presets: [], method: 'auto' };
  let resourceIndex = [], kind = 0, measureTimer = null, measureSequence = 0;
  let resources = [], selected = null, resourceData = null, dirty = false, filter = '', offset = 0;
  let operationResult = '', logs = [], editors = null;
  const drafts = {};
  const enabled = () => !!transport && state.authorized && !state.busy && !pending;
  const panel = body => `<div class="panel"><div class="pscroll">${body}</div></div>`;
  const button = (label, action, primary = false, extra = '') => `<button class="btn${primary ? ' pri' : ''}" data-action="${action}" ${extra} ${enabled() ? '' : 'disabled'}>${label}</button>`;
  const native = index => button('Open full native editor ↗', 'native', false, `data-page="${index}"`);
  const confirmDiscard = () => !dirty || confirm('Discard the unsaved resource text in this editor?');
  function toast(message) {
    $('#toast').textContent = message; $('#toast').style.display = 'block';
    clearTimeout(toast.timer); toast.timer = setTimeout(() => { $('#toast').style.display = 'none'; }, 5000);
  }
  function fit() { $('#stage').style.setProperty('--s', Math.min(innerWidth / 1480, innerHeight / 920, 1.6)); }
  addEventListener('resize', fit); fit();
  addEventListener('beforeunload', event => { if (dirty) { event.preventDefault(); event.returnValue = ''; } });
  function useState(next, preserveResources = false) {
    const pathChanged = next.exePath !== state.exePath;
    const workspaceChanged = next.workspaceRevision !== state.workspaceRevision;
    const updateChanged = JSON.stringify(next.updates) !== JSON.stringify(state.updates);
    const resourcesChanged = next.resourceRevision !== state.resourceRevision;
    state = next;
    if (pathChanged || !state.authorized) {
      palette = { entries: [], hex: [], presets: [], method: 'auto' };
      resources = []; selected = null; resourceData = null; dirty = false;
      editors?.reset(); resourceIndex = []; ++measureSequence; clearTimeout(measureTimer);
    } else if (resourcesChanged && !pending && !preserveResources) {
      resources = []; resourceIndex = []; selected = null; resourceData = null; dirty = false;
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
    document.body.classList.toggle('busy', !!state.busy || pending);
    document.querySelectorAll('[data-action]').forEach(el => {
      el.disabled = !enabled();
    });
  }
  function chrome() {
    const current = pages[page];
    $('#canvas').classList.toggle('page', page !== 0);
    $('#spine').innerHTML = pages.map((p, i) => `<button class="icon${page === i ? ' on' : ''}" data-page="${i}" title="${p[0]}" aria-label="${p[0]}" aria-current="${page === i ? 'page' : 'false'}">${String(i + 1).padStart(2, '0')}</button>`).join('');
    $('#cmd').innerHTML = [0, 1, 2, 11].map(i => `<button class="cmditem${page === i ? ' active' : ''}" data-page="${i}"><strong>${pages[i][1]}</strong></button>`).join('');
    $('#session').className = 'session' + (state.busy || pending ? ' busy' : !state.exeFound ? ' bad' : '');
    $('#session').innerHTML = `<i></i><b>${esc(preview ? 'Design preview' : state.authorized ? 'Discord verified' : 'Sign-in required')}</b><span>${state.busy || pending ? 'WORKING' : state.exeFound ? 'TARGET SET' : 'NO TARGET'}</span>`;
    $('#hero').innerHTML = `<div class="hero"><div class="hero-main"><div class="kicker">BSI workspace / ${esc(current[1])}</div><h1>${esc(current[2])}</h1><p>${esc(current[3])}</p><div class="hero-line"></div></div><div class="target-card"><small>Selected executable</small><b>${esc(state.exePath?.split(/[\\/]/).pop() || 'No target selected')}</b><div class="ready${state.exeFound ? '' : ' bad'}"><i></i>${state.exeFound ? 'File exists · readiness checked by engines' : 'Choose a target in Appearance'}</div></div></div>`;
    $('#stats').innerHTML = page === 0 ? `<div class="stats">${[['Runtime', preview ? 'Preview only' : 'Chromium + C++', 'No localhost server'], ['Session', state.authorized ? 'Verified' : 'Not signed in', 'Existing Discord access rules'], ['Resources', resources.length || '—', 'Manual scans only'], ['Version', state.version || '—', 'Current desktop build']].map(([k, v, hint]) => `<div class="stat"><small>${k}</small><b>${esc(v)}</b><span>${hint}</span></div>`).join('')}</div>` : '';
    $('#tele').innerHTML = `<div class="telemetry"><div class="section-label">Native engine status</div><h2>Always in control.</h2>${[['Backend', transport ? 'Connected locally' : 'Unavailable'], ['Authorization', state.authorized ? 'Discord verified' : 'Required'], ['Operations', state.busy || pending ? 'Working…' : 'Idle'], ['Resource scans', 'Manual only']].map(([k, v]) => `<div class="mini-status"><span>${k}</span><b>${v}</b></div>`).join('')}<div class="divider"></div><p class="note">The interface cannot grant access. Native C++ validates authorization, files, backups and patch limits.</p><div class="button-grid">${button('Full native tools ↗', 'native', false, `data-page="${page}"`)}${button('Sign out', 'signOut')}</div></div>`;
    renderLogs(); markBusy();
  }
  function renderLogs() {
    $('#act').innerHTML = `<div class="activity"><div class="activity-head"><span>NATIVE ACTIVITY</span><button id="clear-logs">Clear</button></div><div id="logs">${logs.length ? logs.slice(-150).reverse().map(([time, message]) => `<div class="log"><i>${esc(time)}</i>${esc(message)}</div>`).join('') : '<div class="log">No native activity yet.</div>'}</div></div>`;
  }
  function renderView() {
    let html = '';
    if (page === 0) {
      html = `<div class="matrix">${[1, 2, 3, 11].map((i, n) => `<button class="tile${n === 0 ? ' primary' : ''}" data-page="${i}"><span class="num">0${n + 1}</span><span class="glyph">↗</span><h3>${pages[i][0]}</h3><p>${esc(pages[i][3])}</p></button>`).join('')}</div>`;
    } else if (page === 1) {
      const rows = palette.method === 'hex' ? palette.hex : palette.entries;
      html = panel(`<div class="sec"><div class="sec-h">Live player palette</div><div class="tools"><select class="inp" id="theme-mode"><option value="auto">Auto detect</option><option value="qml">QML (new)</option><option value="hex">Hex (old)</option></select>${button('Load theme', 'loadTheme', true)}${button('Apply staged colors', 'applyTheme')}${native(1)}</div><p class="note">${esc(state.themeStatus || 'Load the target theme to see real colors. No sample values are patched.')}</p><p class="note">${esc(state.themeDetection || '')}${state.themeStale ? ' · Target changed: reload before applying.' : ''}</p>${palette.presets.length ? `<div class="tools"><select class="inp" id="preset">${palette.presets.map(p => `<option value="${esc(p.id)}">${esc(p.name)}</option>`).join('')}</select>${button('Stage preset', 'preset')}</div>` : ''}<div class="color-list">${rows.length ? rows.map((row, index) => {
        const value = row.edit || row.color || row.value || row.current || '#000000';
        const rgb = /^#[0-9a-f]{6}$/i.test(value) ? value : /^#[0-9a-f]{8}$/i.test(value) ? '#' + value.slice(3) : '#000000';
        return `<div class="color-row"><input type="checkbox" data-color-enabled="${index}" aria-label="Enable ${esc(row.name)}" ${(palette.method === 'hex' ? row.enabled : !!row.edit) ? 'checked' : ''} ${enabled() ? '' : 'disabled'}><div><b>${esc(row.name)}</b><small>${esc(row.purpose)}</small></div><input type="color" value="${esc(rgb)}" data-color="${index}" aria-label="Pick ${esc(row.name)}" ${enabled() ? '' : 'disabled'}><input class="inp mono" value="${esc(value)}" data-color="${index}" aria-label="Value for ${esc(row.name)}" ${enabled() ? '' : 'disabled'}></div>`;
      }).join('') : '<div class="empty">No palette loaded. Choose an executable, then click Load theme.</div>'}</div></div>`);
    } else if (page === 2) {
      html = panel(`<div class="sec"><div class="sec-h">Resource explorer</div><div class="tools">${button('Scan Resources', 'scanResources', true)}${native(2)}<input class="inp grow" id="resource-filter" placeholder="Search paths (space-separated terms)" value="${esc(filter)}" aria-label="Search resource paths"></div><p class="note">Opening this page never scans. The backend enforces encoded storage capacity and refuses unsafe writes.</p><label class="editor-field"><span>Resource kind</span><select class="inp" id="resource-kind">${['All', 'Pictures', 'SVG', 'Code', 'Binary', 'Fonts', 'Modified vs backup'].map((label, index) => `<option value="${index}" ${kind === index ? 'selected' : ''}>${label}</option>`).join('')}</select></label><p class="note">Search supports quoted phrases and min: / max: byte sizes (e.g. min:2kb).</p><div id="resource-rows"></div><div id="resource-editor"></div></div>`);
    } else if ([3, 4, 5].includes(page)) {
      const commands = page === 3 ? [['Detect FPS target', 'fpsDetect'], ['Analyze FPS', 'fpsAnalyze'], ['Apply FPS limit', 'fpsPatch'], ['Restore FPS', 'fpsRestore']]
        : page === 4 ? [['Check integrity', 'integrityCheck'], ['Apply integrity patch', 'integrityPatch'], ['Restore integrity', 'integrityRestore']]
        : [['Apply boot patch', 'bootPatch'], ['Purge player cache', 'purgePlayerCache']];
      html = panel(`<div class="sec"><div class="sec-h">${pages[page][0]}</div><p class="native-note">These commands call the existing engines directly. Close all emulator instances before changing files. Backups and validation stay in C++.</p>${page === 3 ? `<label class="lbl" for="fps-limit">FPS limit</label><input id="fps-limit" class="inp" type="number" min="1" max="10000" data-draft="fps" value="${esc(drafts.fps || '240')}">` : page === 5 ? `<label class="lbl" for="boot-hold">Boot hold (milliseconds)</label><input id="boot-hold" class="inp" type="number" min="0" max="600000" data-draft="hold" value="${esc(drafts.hold || '3000')}">` : ''}<div class="button-grid">${commands.map(([label, action]) => button(label, action)).join('')}${native(page)}</div><pre id="operation-result"></pre></div>`);
    } else if (page === 8) {
      html = panel(`<div class="sec"><div class="sec-h">Application appearance</div><div class="tools"><select class="inp" id="appearance-mode" aria-label="App theme">${['Dark', 'Light', 'System'].map((v, i) => `<option value="${i}" ${state.appearance === i ? 'selected' : ''}>${v}</option>`).join('')}</select><input type="color" id="accent" aria-label="App accent" value="${esc(state.accent || '#9ce9c1')}">${button('Save appearance', 'appearance', true)}</div></div><div class="sec"><div class="sec-h">Native targets</div>${['exe', 'dll', 'conf'].map(role => `<label class="lbl" for="path-${role}">${{ exe: 'HD-Player.exe', dll: 'Qt5/Qt6 QuickTemplates2 DLL', conf: 'bluestacks.conf' }[role]}</label><div class="path-row"><input class="inp mono" id="path-${role}" value="${esc(state[role + 'Path'] || '')}">${button('Browse…', 'browse', false, `data-role="${role}"`)}${button('Set path', 'path', false, `data-role="${role}"`)}</div>`).join('')}<div class="button-grid">${button('Detect installations', 'detectInstalls')}${button('Restore EXE backup', 'restoreBackup')}</div>${(state.installs || []).map((install, index) => `<div class="kv"><span>${esc(install.profile || install.root)}</span>${button('Use installation', 'useInstall', false, `data-index="${index}"`)}</div>`).join('')}</div>`);
    } else {
      html = panel(editors.render(page));
    }
    if (page === 0) html = panel('<div class="dashboard-scroll">' + html + editors.extra(page) + '</div>');
    $('#main').innerHTML = html;
    if (page !== 0) $('#main .pscroll')?.insertAdjacentHTML('beforeend', editors.extra(page));
    if (page === 2) { renderResources(); renderResourceEditor(); }
    if ($('#operation-result')) $('#operation-result').textContent = operationResult;
    markBusy();
  }
  function renderResources() {
    if (!$('#resource-rows')) return;
    const rows = BsiResourceSearch.filter(resourceIndex, filter, kind);
    offset = Math.max(0, Math.min(offset, Math.max(0, Math.ceil(rows.length / 100) - 1) * 100));
    $('#resource-rows').innerHTML = `<p class="note">${rows.length} matching / ${resources.length} scanned · ${resources.length ? 'Showing ' + (offset + 1) + '–' + Math.min(offset + 100, rows.length) : 'Click Scan Resources to begin'}</p><div class="resource-list">${rows.slice(offset, offset + 100).map(r => `<button class="resource-row${r.index === selected?.index ? ' selected' : ''}" data-resource="${r.index}" ${enabled() ? '' : 'disabled'}><small>${esc(r.kind)} · ${esc(r.size)} B</small>${esc(r.path)}</button>`).join('')}</div>${rows.length > 100 ? `<div class="tools"><button class="btn sm" data-paginate="-1" ${offset ? '' : 'disabled'}>Previous</button><button class="btn sm" data-paginate="1" ${offset + 100 < rows.length ? '' : 'disabled'}>Next</button></div>` : ''}`;
  }
  function renderResourceEditor() {
    if (!$('#resource-editor')) return;
    const box = $('#resource-editor');
    if (!selected || !resourceData) { box.innerHTML = '<div class="empty">Select a scanned resource to inspect it.</div>'; return; }
    box.innerHTML = `<h3>${esc(selected.path)}</h3><p class="note" id="resource-budget"></p>${selected.text ? '<textarea class="inp" id="resource-code" spellcheck="false" aria-label="Resource source text"></textarea>' : '<img id="resource-image" alt="Resource preview" hidden><p class="note" id="resource-note"></p>'}<div class="button-grid">${selected.text ? button('Save resource text', 'saveResource', true) : ''}${button('Replace with file…', 'replaceResource')}${button('Extract…', 'extractResource')}</div>`;
    const budget = resourceData.budget || {};
    $('#resource-budget').textContent = `Decoded: ${selected.size} B · Stored: ${selected.stored} B · Capacity: ${budget.known ? budget.capacity + ' B' : 'unknown'} · ${budget.msg || 'Validated by native engine on save.'}`;
    if ($('#resource-code')) $('#resource-code').value = resourceData.text || '';
    if ($('#resource-image')) {
      if (/^data:image\/(png|jpeg|webp|gif|bmp);base64,/i.test(resourceData.preview || '')) {
        $('#resource-image').src = resourceData.preview; $('#resource-image').hidden = false;
      }
      $('#resource-note').textContent = resourceData.note || 'No raster preview. Use the full native editor for additional formats.';
    }
  }
  function scheduleMeasurement() {
    const sequence = ++measureSequence;
    clearTimeout(measureTimer);
    if (!selected || !resourceData || !enabled()) return;
    const index = selected.index, text = resourceData.text, path = state.exePath;
    measureTimer = setTimeout(async () => {
      if (!enabled() || sequence !== measureSequence) return;
      try {
        // Native measurement uses cached compression metadata; never scans or saves.
        const size = await transport.invoke('measureResource', { index, text, bom: !!resourceData?.budget?.bom });
        if (sequence !== measureSequence || selected?.index !== index || state.exePath !== path || !$('#resource-budget')) return;
        const capacity = resourceData.budget?.capacity;
        $('#resource-budget').textContent = `Edited decoded: ${size.decoded} B · Encoded payload: ${size.encoded} B · Capacity: ${capacity ?? 'unknown'} B · ${size.encoded > capacity ? 'Needs verified relocation; native save may refuse.' : 'Native validation still required on save.'}`;
      } catch (_) { /* Busy or changed selection: never guess an encoded budget. */ }
    }, 300);
  }
  async function request(method, args = {}) {
    if (!enabled()) throw new Error('Native operations require the desktop runtime, a verified Discord session and an idle engine.');
    pending = true; chrome();
    try {
      const value = await transport.invoke(method, args);
      operationResult = typeof value === 'string' ? value : JSON.stringify(value ?? 'Native request completed.', null, 2);
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
    resourceIndex = BsiResourceSearch.prepare(resources);
  }
  async function navigate(index) {
    if (!Number.isInteger(index) || index < 0 || index >= pages.length || index === page) return;
    if (pending || state.busy || !confirmDiscard()) return;
    if (dirty) { selected = null; resourceData = null; }
    dirty = false; page = index; operationResult = ''; chrome(); renderView();
    // Reads only the already-loaded palette. Neither this nor Resources navigation scans files.
    if (page === 1 && enabled()) { await readPalette(); renderView(); }
    await editors.navigate(page);
    if ([5, 10, 11].includes(page)) renderView();
  }
  async function action(el) {
    const name = el.dataset.action;
    if (!enabled()) return;
    if (name.startsWith('editor:')) { await editors.action(name.slice(7), el, page); renderView(); return; }
    if (name === 'native') { if (confirmDiscard()) { if (dirty) { selected = null; resourceData = null; } dirty = false; await request('openNativeTool', { page: Number(el.dataset.page) }); } return; }
    if (name === 'loadTheme') { await request('loadTheme', { mode: $('#theme-mode').value }); await readPalette(); }
    else if (name === 'applyTheme') { if (!confirm('Apply staged colors to the selected EXE using the native engine?')) return; await request(name); await readPalette(); }
    else if (name === 'preset') { await request('applyPreset', { id: $('#preset').value }); await readPalette(); }
    else if (name === 'scanResources') { if (!confirmDiscard()) return; dirty = false; selected = null; resourceData = null; await request(name); await refreshResources(); }
    else if (name === 'saveResource') { if (!confirm('Save this resource into the selected EXE? The native engine will validate storage capacity.')) return; await request(name, { index: selected.index, text: resourceData.text }); dirty = false; await refreshResources(); resourceData = await request('readResource', { index: selected.index }); }
    else if (name === 'replaceResource' || name === 'extractResource') {
      if (name === 'replaceResource' && !confirmDiscard()) return;
      const path = await transport.chooseFile(name === 'replaceResource' ? 'replace' : 'extract');
      if (!path) return;
      if (name === 'replaceResource' && !confirm('Replace the selected EXE resource with this file?')) return;
      await request(name, { index: selected.index, path });
      if (name === 'replaceResource') { dirty = false; await refreshResources(); resourceData = await request('readResource', { index: selected.index }); }
    } else if (name === 'browse') { if (!confirmDiscard()) return; const path = await transport.chooseFile(el.dataset.role); if (!path) return; }
    else if (name === 'path') { if (!confirmDiscard()) return; const role = el.dataset.role; await request('setPaths', { [role]: $('#path-' + role).value }); }
    else if (name === 'appearance') await request('setAppearance', { theme: Number($('#appearance-mode').value), accent: $('#accent').value });
    else if (name === 'detectInstalls') await request(name);
    else if (name === 'useInstall') { if (!confirmDiscard()) return; await request(name, { index: Number(el.dataset.index) }); }
    else if (name === 'signOut') { if (confirmDiscard()) await request(name); return; }
    else {
      const mutating = ['fpsPatch', 'fpsRestore', 'integrityPatch', 'integrityRestore', 'bootPatch', 'purgePlayerCache', 'restoreBackup'].includes(name);
      if (mutating && !confirm('Run ' + name + ' on the selected target? Close all emulator instances first.')) return;
      await request(name, name === 'fpsPatch' ? { limit: Number($('#fps-limit').value) } : name === 'bootPatch' ? { holdMs: Number($('#boot-hold').value) } : {});
    }
    renderView();
  }
  document.addEventListener('click', async event => {
    try {
      const actionEl = event.target.closest('[data-action]');
      if (actionEl) { await action(actionEl); return; }
      const nav = event.target.closest('[data-page]');
      if (nav) { await navigate(Number(nav.dataset.page)); return; }
      const row = event.target.closest('[data-resource]');
      if (row && enabled() && confirmDiscard()) {
        const index = Number(row.dataset.resource);
        const data = await request('readResource', { index });
        ++measureSequence; clearTimeout(measureTimer);
        selected = resources.find(r => r.index === index); resourceData = data; dirty = false;
        renderResources(); renderResourceEditor(); return;
      }
      const paginate = event.target.closest('[data-paginate]');
      if (paginate) { offset += Number(paginate.dataset.paginate) * 100; renderResources(); }
      if (event.target.closest('#clear-logs')) { logs = []; renderLogs(); }
    } catch (error) { toast(error.message || String(error)); renderView(); }
  });
  document.addEventListener('input', event => {
    const el = event.target;
    editors.input(el);
    if (el.id === 'resource-filter') { filter = el.value; offset = 0; renderResources(); }
    if (el.id === 'resource-code' && resourceData) { resourceData.text = el.value; dirty = true; scheduleMeasurement(); }
    if (el.dataset.draft) drafts[el.dataset.draft] = el.value;
  });
  document.addEventListener('change', async event => {
    const el = event.target;
    editors.input(el);
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
  async function start() {
    chrome(); renderView();
    try {
      transport = await BsiTransport.connect();
      transport.onState(useState);
      transport.onLog(message => { logs.push([new Date().toLocaleTimeString(), message]); if (logs.length > 500) logs.shift(); renderLogs(); });
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
    invalidateResources: () => { resources = []; resourceIndex = []; selected = null; resourceData = null; dirty = false; ++measureSequence; clearTimeout(measureTimer); },
    setBootHold: hold => { drafts.hold = String(hold); }
  });
  start();
})();
