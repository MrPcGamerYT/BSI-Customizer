/* BSI Chromium workspace. Native authentication and engines remain the authority. */
(() => {
  'use strict';
  const $ = s => document.querySelector(s);
  const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  const pages = [
    ['Overview', 'Dashboard', 'Customize your emulator.', 'Precision tools for themes, removals and advanced BlueStacks tweaks.'],
    ['Theme studio', 'Colors', 'Color studio', 'Load the real player palette, stage edits, then apply.'],
    ['Resource explorer', 'Resources', 'Inside your player.', 'Explicit scans only. Inspect, edit and extract embedded resources.'],
    ['Performance', 'FPS', 'More room to perform.', 'The same native FPS detection, analysis and patch engines.'],
    ['Integrity & safety', 'Integrity', 'Inspect before you change.', 'Native integrity checks, patching and restore.'],
    ['Boot experience', 'Boot', 'Make an entrance.', 'Configure the native boot patch or open the complete editor.'],
    ['Player interface', 'Interface', 'Less noise. More you.', 'Removals, layout tweaks, the in-player Settings redesign and editable About pages.'],
    ['Discord presence', 'Presence', 'Your player, represented.', 'Keep the existing Rich Presence ID and version-resource utilities.'],
    ['Appearance', 'Settings', 'Your workspace.', 'App appearance and target paths. These are not player theme colors.'],
    ['About BSI', 'About', 'Built around your player.', 'Chromium presentation. C++ patch engines. Existing login and update verification.'],
    ['Animated presence', 'Animated', 'Bring presence to life.', 'Configure the existing native GIF RPC plugin and restore flow.'],
    ['Patch workspace', 'Workspace', 'One deliberate change.', 'Portable profiles, queued changes, dry runs and recoverable batch apply.'],
    ['Debloat & Android', 'Debloat', 'A cleaner player.', 'Original configuration cleanup, Android ADB and journalled Root.vhd tools.']
  ];
  const icons = [`<rect x="3" y="3" width="7" height="7" rx="1"/><rect x="14" y="3" width="7" height="7" rx="1"/><rect x="3" y="14" width="7" height="7" rx="1"/><rect x="14" y="14" width="7" height="7" rx="1"/>`, `<circle cx="12" cy="12" r="3"/><path d="M12 3v3M12 18v3M3 12h3M18 12h3M5.6 5.6l2.1 2.1M16.3 16.3l2.1 2.1"/>`, `<rect x="4" y="3" width="16" height="18" rx="2"/><path d="M8 8h8M8 12h8M8 16h4"/>`, `<path d="m4 16 5-7 4 4 7-9M15 4h5v5"/>`, `<path d="m12 3 8 3v6c0 5-8 9-8 9s-8-4-8-9V6zM8 12l3 3 5-6"/>`, `<rect x="3" y="5" width="18" height="14" rx="2"/><path d="m10 9 5 3-5 3z"/>`, `<path d="M5 7h14M5 12h14M5 17h14"/><circle cx="8" cy="7" r="1.5"/><circle cx="15" cy="12" r="1.5"/>`, `<path d="M4 5h16v12H8l-4 4zM8 10h8M8 13h5"/>`, `<circle cx="12" cy="12" r="4"/><path d="M12 3v3M12 18v3M3 12h3M18 12h3M5 5l2 2M17 17l2 2M5 19l2-2M17 7l2-2"/>`, `<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7v1"/>`, `<path d="m12 3 2.3 5.2L20 9l-4 4 .9 5.7-4.9-2.6-4.9 2.6L8 13 4 9l5.7-.8z"/>`, `<rect x="3" y="6" width="18" height="15" rx="2"/><path d="M8 6V3h8v3M3 11h18M10 11v3h4v-3"/>`];
  icons.push(`<path d="M4 6h16M6 6l1 14h10l1-14M9 6V3h6v3M10 10v6M14 10v6"/>`);
  const svg = i => `<svg viewBox="0 0 24 24" aria-hidden="true">${icons[i]}</svg>`;
  let transport = null, preview = false, state = {}, page = 0, pending = false;
  let palette = { entries: [], hex: [], presets: [], method: 'auto' };
  let resourceIndex = [], kind = 0, measureTimer = null, measureSequence = 0;
  let resources = [], selected = null, resourceData = null, dirty = false, filter = '', offset = 0;
  let operationResult = '', logs = [], editors = null, windowMaximized = false;
  const drafts = {};
  let bootLogoPreview = '';
  const enabled = () => !!transport && state.authorized && !state.busy && !pending;
  const panel = body => `<div class="panel"><div class="pscroll">${body}</div></div>`;
  const button = (label, action, primary = false, extra = '') => `<button class="btn${primary ? ' pri' : ''}" data-action="${action}" data-locked="${/^\s*disabled(?:\s|$)/.test(extra)}" ${extra} ${enabled() ? '' : 'disabled'}>${label}</button>`;
  const native = () => ''; // No second editor in the normal HTML workflow.
  const confirmDiscard = async () => !dirty || BsiEffects.confirm('Discard the unsaved resource text in this editor?');
  function toast(message, kind = 'error') {
    const element = $('#toast');
    element.textContent = message;
    element.dataset.kind = kind;
    element.style.display = 'block';
    clearTimeout(toast.timer); toast.timer = setTimeout(() => { element.style.display = 'none'; }, 7000);
  }
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
      bootLogoPreview = '';
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
    document.querySelectorAll('[data-window]').forEach(el => {
      el.disabled = !transport?.windowAction || (el.dataset.window === 'close' && (!!state.busy || pending));
    });
    document.querySelectorAll('[data-action]').forEach(el => {
      el.disabled = !enabled() || el.dataset.locked === 'true';
    });
  }
  function targetName() {
    const install = (state.installs || []).find(i => i.exe === state.exePath || i.exePath === state.exePath);
    return install?.profile || (state.exeFound ? 'BlueStacks Player' : 'No target selected');
  }
  const settingsIcons = {
    appearance: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.9l.1.1-1.8 1.8-.1-.1a1.7 1.7 0 0 0-1.9-.3 1.7 1.7 0 0 0-1 1.5v.1h-2.6v-.1a1.7 1.7 0 0 0-1-1.5 1.7 1.7 0 0 0-1.9.3l-.1.1-1.8-1.8.1-.1a1.7 1.7 0 0 0 .3-1.9 1.7 1.7 0 0 0-1.5-1H6v-2.6h.1a1.7 1.7 0 0 0 1.5-1 1.7 1.7 0 0 0-.3-1.9l-.1-.1L9 6.6l.1.1a1.7 1.7 0 0 0 1.9.3 1.7 1.7 0 0 0 1-1.5v-.1h2.6v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.9-.3l.1-.1 1.8 1.8-.1.1a1.7 1.7 0 0 0-.3 1.9 1.7 1.7 0 0 0 1.5 1h.1V14h-.1a1.7 1.7 0 0 0-1.1 1z"/></svg>',
    updates: '<svg viewBox="0 0 24 24"><path d="M20 7v5h-5M4 17v-5h5"/><path d="M5.6 9a7 7 0 0 1 11.7-2L20 12M4 12l2.7 5a7 7 0 0 0 11.7-2"/></svg>',
    about: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7v1"/></svg>'
  };
  let settingsTab = 'appearance';
  function settingsAppearance() {
    return `<div class="settings-section-head"><span class="settings-eyebrow">WORKSPACE</span><h3>Appearance & installation</h3><p>Choose how BSI looks and which BlueStacks files its tools should use.</p></div>
      <section class="settings-card"><div class="settings-card-head"><div><b>Color mode</b><p>Choose a comfortable look for the app.</p></div><span class="settings-badge">SAVES INSTANTLY</span></div><div class="settings-mode-row"><label class="editor-field"><span>Theme</span><select class="inp" id="appearance-mode" aria-label="App theme">${['Dark','Light','System'].map((name,i)=>`<option value="${i}" ${state.appearance===i?'selected':''}>${name}</option>`).join('')}</select></label><label class="editor-field"><span>Accent color</span><input type="color" class="inp settings-color" id="accent" aria-label="App accent" value="${esc(state.accent||'#9ce9c1')}"></label>${button('Save appearance','appearance',true)}</div></section>
      <section class="settings-card"><div class="settings-card-head"><div><b>BlueStacks locations</b><p>Set the player executable, helper DLL and configuration file.</p></div></div>${[['exe','HD-Player.exe'],['dll','QuickTemplates2 DLL'],['conf','bluestacks.conf']].map(([role,label])=>`<div class="settings-path"><label class="editor-field" for="path-${role}"><span>${label}</span><input class="inp mono" id="path-${role}" value="${esc(state[role+'Path']||'')}"></label>${button('Browse…','browse',false,`data-role="${role}"`)}${button('Set path','path',false,`data-role="${role}"`)}</div>`).join('')}<div class="button-grid">${button('Detect installations','detectInstalls')}${button('Restore EXE backup','restoreBackup')}</div>${(state.installs||[]).map((install,index)=>`<div class="settings-install"><span>${esc(install.profile||install.root)}</span>${button('Use installation','useInstall',false,`data-index="${index}"`)}</div>`).join('')}</section>`;
  }
  function settingsAboutData() {
    const profile = state.aboutProfile || {};
    const links = Array.isArray(profile.links) ? profile.links : [];
    return {
      developerName: profile.developerName || 'Mr.Pc Gamer & Romulos',
      tagline: profile.tagline || 'BlueStacks tools · with contributor Nova',
      links: Array.from({length:4}, (_, i) => ({ kind: links[i]?.kind || ['github','discord','youtube','youtube'][i], title: links[i]?.title || '', url: links[i]?.url || '' }))
    };
  }
  function aboutPreview(data = settingsAboutData()) {
    const links = data.links.filter(link => link.title && link.url);
    return `<div class="about-profile-preview"><img src="icon.png" alt="BSI Customizer emulator icon"><b>BSI CUSTOMIZER</b><h3>${esc(data.developerName)}</h3><p>${esc(data.tagline||'')}</p><small>Version ${esc(state.version||'—')}</small><div class="about-profile-links">${links.map(link=>`<span class="about-link-chip"><i>${link.kind==='discord'?'D':link.kind==='youtube'?'▶':'GH'}</i>${esc(link.title)}</span>`).join('')||'<span class="about-empty">Add up to four links below</span>'}</div></div>`;
  }
  function settingsAbout() {
    const data = settingsAboutData();
    return `<div class="settings-section-head"><span class="settings-eyebrow">YOUR PROFILE</span><h3>About BSI Customizer</h3><p>Build the About card shown in this app. Your profile is saved on this PC.</p></div>
      <div id="about-profile-preview-host">${aboutPreview(data)}</div>
      <section class="settings-card about-profile-editor"><div class="settings-card-head"><div><b>Profile details</b><p>Your name, short description, and four optional social links.</p></div></div>
      <div class="settings-about-fields"><label class="editor-field"><span>Developer name</span><input class="inp" maxlength="80" data-about-field="developerName" value="${esc(data.developerName)}"></label><label class="editor-field"><span>Tagline</span><input class="inp" maxlength="120" data-about-field="tagline" value="${esc(data.tagline)}"></label></div>
      <div class="settings-link-list">${data.links.map((link,index)=>`<div class="settings-link-row" data-about-link="${index}"><span class="settings-link-index">0${index+1}</span><label class="editor-field"><span>Service</span><select class="inp" data-about-field="kind"><option value="github" ${link.kind==='github'?'selected':''}>GitHub</option><option value="discord" ${link.kind==='discord'?'selected':''}>Discord</option><option value="youtube" ${link.kind==='youtube'?'selected':''}>YouTube</option></select></label><label class="editor-field"><span>Link label</span><input class="inp" maxlength="64" data-about-field="title" value="${esc(link.title)}" placeholder="e.g. Project source"></label><label class="editor-field"><span>HTTP(S) URL</span><input class="inp" maxlength="512" data-about-field="url" value="${esc(link.url)}" placeholder="https://…"></label></div>`).join('')}</div>
      <div class="button-grid">${button('Save About profile','settings-about-save',true)}${button('Restore defaults','settings-about-reset')}</div></section>`;
  }
  function renderSettingsTab() {
    const dialog = $('#settings-dialog');
    if (!dialog) return;
    document.querySelectorAll('[data-settings-tab]').forEach(tab => {
      const active = tab.dataset.settingsTab === settingsTab;
      tab.classList.toggle('active', active);
      tab.setAttribute('aria-selected', String(active));
      const icon = tab.querySelector('.settings-tab-icon');
      if (icon) icon.innerHTML = settingsIcons[tab.dataset.settingsTab] || '';
    });
    const content = $('#settings-content');
    content.innerHTML = settingsTab === 'appearance' ? settingsAppearance()
      : settingsTab === 'updates' ? `<div class="settings-section-head"><span class="settings-eyebrow">RELEASES</span><h3>Updates & recovery</h3><p>Review version, trust and download state before choosing an update action.</p></div>${editors.settingsUpdates()}`
      : settingsAbout();
    markBusy();
  }
  function openSettings(tab = settingsTab) {
    if (!['appearance','updates','about'].includes(tab)) tab = 'appearance';
    settingsTab = tab;
    const dialog = $('#settings-dialog');
    if (!dialog.open) dialog.showModal();
    renderSettingsTab();
  }
  function refreshVisibleView() {
    if ($('#settings-dialog')?.open) renderSettingsTab();
    else renderView();
  }
  function updateAboutPreviewFromForm() {
    const host = $('#about-profile-preview-host');
    if (!host) return;
    const read = (name, fallback='') => host.closest('#settings-dialog')?.querySelector(`[data-about-field="${name}"]`)?.value ?? fallback;
    const links = Array.from(document.querySelectorAll('[data-about-link]')).map(row => ({
      kind: row.querySelector('[data-about-field="kind"]')?.value || 'github',
      title: row.querySelector('[data-about-field="title"]')?.value || '',
      url: row.querySelector('[data-about-field="url"]')?.value || ''
    }));
    host.innerHTML = aboutPreview({developerName:read('developerName','Your name'),tagline:read('tagline',''),links});
  }
  function updateWindowMode(maximized) {
    windowMaximized = !!maximized;
    const button = document.querySelector('[data-window="max"]');
    if (!button) return;
    button.title = windowMaximized ? 'Restore down' : 'Maximize';
    button.setAttribute('aria-label', button.title);
    button.dataset.maximized = String(windowMaximized);
    button.innerHTML = windowMaximized
      ? '<svg viewBox="0 0 12 12" aria-hidden="true"><path d="M4.5 2.5h5v5"/><path d="M8.5 4.5v5h-5v-5z"/></svg>'
      : '<svg viewBox="0 0 12 12" aria-hidden="true"><rect x="2.5" y="2.5" width="7" height="7" rx="1.6"/></svg>';
  }
  function chrome() {
    const current = pages[page];
    $('#canvas').classList.toggle('page', page !== 0);
    const groups = [
      ['HOME', [0]], ['CUSTOMIZE', [1, 2, 3, 4, 5, 6]],
      ['PRESENCE', [7, 10]], ['APP', [8, 9]], ['TOOLS', [11, 12]]
    ];
    $('#spine').innerHTML = groups.map(([label, items]) => `<div class="nav-group"><span class="nav-group-label">${label}</span>${items.map(i => `<button class="icon${page === i ? ' on' : ''}" data-page="${i}" aria-label="${pages[i][0]}" aria-current="${page === i ? 'page' : 'false'}"><span class="nav-glyph">${svg(i)}</span><span class="nav-label">${esc(pages[i][0])}</span></button>`).join('')}</div>`).join('');
    $('#session').className = 'session' + (state.busy || pending ? ' busy' : !state.exeFound ? ' bad' : '');
    $('#session').innerHTML = `<i></i><b>${esc(preview ? 'Design preview' : state.authorized ? 'Discord verified' : 'Sign-in required')}</b><span>${state.busy || pending ? 'WORKING' : state.exeFound ? 'READY' : 'NO TARGET'}</span><em></em>`;
    $('#hero').innerHTML = `<div class="hero"><div class="hero-main"><div class="kicker">Control center / ${esc(current[1]).toLowerCase()}</div><h1>${esc(current[2])}</h1><p>${esc(current[3])}</p><div class="hero-line"></div></div><div class="target-card"><small>Active target</small><b>${esc(targetName())}</b><div class="ready${state.exeFound ? '' : ' bad'}"><i></i>${state.exeFound ? 'HD-Player.exe detected' : 'Select your player in Settings'}</div></div></div>`;
    $('#profile-options').innerHTML = `<div class="pd-h">Target installations</div>${(state.installs || []).map((install, index) => `<button class="pd-opt" data-action="useInstall" data-index="${index}" ${enabled() ? '' : 'disabled'}><i></i>${esc(install.profile || install.root)}</button>`).join('') || '<p class="note">Detect your installations in Settings.</p>'}<button class="pd-opt" data-page="8">Manage targets ↗</button>`;
    $('#stats').innerHTML = page === 0 ? `<div class="stats">${[['Profile', targetName(), 'Active target'], ['Executable', state.exeFound ? 'Found' : 'Missing', 'HD-Player.exe'], ['Colors', palette.entries.filter(c => !!c.edit).length, 'Staged edits'], ['Resources', resources.length || '—', 'Explicit scans only']].map(([k, v, hint]) => `<div class="stat"><small>${k}</small><b>${esc(v)}</b><span>${hint}</span></div>`).join('')}</div>` : '';
    const meters = [['Colors staged', palette.entries.filter(c => !!c.edit).length, palette.entries.length], ['Resources loaded', resources.length, Math.max(resources.length, 1)], ['Target selected', state.exeFound ? 1 : 0, 1]];
    $('#tele').innerHTML = `<div class="telemetry"><div class="section-label">Live overview</div><h2>System state</h2>${meters.map(([label, value, total], i) => `<div class="meter"><div class="meter-top"><span>${label}</span><b>${i === 2 ? (value ? 'Ready' : 'Not set') : value + ' / ' + total}</b></div><div class="bar ${i === 1 ? 'blue' : i === 2 ? 'white' : ''}"><i style="width:${total ? Math.min(100, value / total * 100) : 0}%"></i></div></div>`).join('')}<div class="divider"></div>${[['Session', preview ? 'Preview only' : state.authorized ? 'Discord verified' : 'Required'], ['Engine', state.busy || pending ? 'Working…' : 'Idle'], ['Version', state.version || '—']].map(([k, v]) => `<div class="mini-status"><span>${k}</span><b>${esc(v)}</b></div>`).join('')}<div class="tele-actions">${button('OPEN PATCH WORKSPACE', 'workspaceHome', true)}${button('RESTORE ORIGINAL', 'restoreBackup')}<div class="tools">${button('Sign out', 'signOut')}</div></div></div>`;
    updateWindowMode(windowMaximized);
    renderLogs(); markBusy();
  }
  function renderLogs() {
    $('#act').innerHTML = `<div class="activity"><div class="activity-head"><span>RECENT ACTIVITY</span><button id="clear-logs">Clear</button></div><div id="logs">${logs.length ? logs.slice(-150).reverse().map(([time, message]) => `<div class="log"><i>${esc(time)}</i>${esc(message)}</div>`).join('') : '<div class="log">No native activity yet.</div>'}</div></div>`;
  }
  function renderView() {
    let html = '';
    if (page === 0) {
      html = `<div class="matrix">${[[11, 'Patch workspace', 'Plan, validate and apply your changes safely.'], [1, 'Color studio', 'Edit your player palette and stage live selections.'], [6, 'UI removals', 'Select unwanted emulator controls.'], [8, 'Instance control', 'Manage targets, instances and player paths.']].map(([i, title, desc], n) => `<button class="tile${n === 0 ? ' primary' : ''}" data-page="${i}"><span class="num">0${n + 1}</span><span class="glyph">${svg(i)}</span><h3>${title}</h3><p>${desc}</p></button>`).join('')}</div>`;
    } else if (page === 1) {
      const rows = palette.method === 'hex' ? palette.hex : palette.entries;
      html = panel(`<div class="sec"><div class="sec-h">Live player palette</div><div class="tools"><select class="inp" id="theme-mode"><option value="auto">Auto detect</option><option value="qml">QML (new)</option><option value="hex">Hex (old)</option></select>${button('Load theme', 'loadTheme', true)}${button('Apply staged colors', 'applyTheme')}${native(1)}</div><p class="note">${esc(state.themeStatus || 'Load the target theme to see real colors. No sample values are patched.')}</p><p class="note">${esc(state.themeDetection || '')}${state.themeStale ? ' · Target changed: reload before applying.' : ''}</p>${palette.presets.length ? `<div class="tools"><select class="inp" id="preset">${palette.presets.map(p => `<option value="${esc(p.id)}">${esc(p.name)}</option>`).join('')}</select>${button('Stage preset', 'preset')}</div>` : ''}<div class="color-list">${rows.length ? rows.map((row, index) => {
        const value = row.edit || row.color || row.value || row.current || '#000000';
        const rgb = /^#[0-9a-f]{6}$/i.test(value) ? value : /^#[0-9a-f]{8}$/i.test(value) ? '#' + value.slice(3) : '#000000';
        return `<div class="color-row"><input type="checkbox" data-color-enabled="${index}" aria-label="Enable ${esc(row.name)}" ${(palette.method === 'hex' ? row.enabled : !!row.edit) ? 'checked' : ''} ${enabled() ? '' : 'disabled'}><div><b>${esc(row.name)}</b><small>${esc(row.purpose)}</small></div><input type="color" value="${esc(rgb)}" data-color="${index}" aria-label="Pick ${esc(row.name)}" ${enabled() ? '' : 'disabled'}><input class="inp mono" value="${esc(value)}" data-color="${index}" aria-label="Value for ${esc(row.name)}" ${enabled() ? '' : 'disabled'}></div>`;
      }).join('') : '<div class="empty">No palette loaded. Choose an executable, then click Load theme.</div>'}</div></div>`);
    } else if (page === 2) {
      html = panel(`<div class="sec"><div class="sec-h">Resource explorer</div><div class="tools">${button('Scan Resources', 'scanResources', true)}${native(2)}<input class="inp grow" id="resource-filter" placeholder="Search paths (space-separated terms)" value="${esc(filter)}" aria-label="Search resource paths"></div><p class="note">Opening this page never scans. The backend enforces encoded storage capacity and refuses unsafe writes.</p><label class="editor-field"><span>Show</span><select class="inp" id="resource-kind">${['All files', 'Pictures', 'SVG', 'Code', 'Binary', 'Fonts', 'Modified vs backup'].map((label, index) => `<option value="${index}" ${kind === index ? 'selected' : ''}>${label}</option>`).join('')}</select></label><p class="note">Search supports quoted phrases and min: / max: byte sizes (e.g. min:2kb).</p><div class="resource-browser"><div id="resource-rows"></div><div id="resource-editor"></div></div></div>`);
    } else if ([3, 4, 5].includes(page)) {
      const commands = page === 3 ? [['Detect FPS target', 'fpsDetect'], ['Analyze FPS', 'fpsAnalyze'], ['Apply FPS limit', 'fpsPatch'], ['Restore FPS', 'fpsRestore']]
        : page === 4 ? [['Check integrity', 'integrityCheck'], ['Apply integrity patch', 'integrityPatch'], ['Restore integrity', 'integrityRestore']]
        : [[(drafts.bootMode || 'modern') === 'video' ? 'Install Custom Video Boot' : 'Install Animated Modern Boot', 'bootPatch'], ['Purge player cache', 'purgePlayerCache']];
      const bootOptions = page === 5 ? `<label class="editor-field"><span>Boot style</span><select class="inp" id="boot-mode" data-draft="bootMode"><option value="modern" ${(drafts.bootMode || 'modern') === 'modern' ? 'selected' : ''}>Built-in modern boot</option><option value="video" ${drafts.bootMode === 'video' ? 'selected' : ''}>Custom MP4</option></select></label><div class="boot-preview" aria-label="Animated emulator boot preview"><div class="boot-glow"></div><div class="boot-ring"><div class="boot-orbit"></div>${bootLogoPreview ? `<img class="boot-logo" src="${esc(bootLogoPreview)}" alt="">` : '<div class="boot-logo-placeholder" aria-hidden="true"></div>'}</div><span class="boot-status">Emulator is starting…</span></div>${drafts.bootMode === 'video' ? `<div class="tools">${button('Choose MP4…', 'bootVideoBrowse')}<span id="boot-video-selected" class="note grow">${esc(drafts.bootVideo || 'No MP4 selected')}</span></div>` : ''}<p class="note">The player’s own BlueStacks logo sits inside a softly glowing loading ring. The only added status is “Emulator is starting…”. The original EXE is backed up; custom video and helper updates are reversible.</p>` : '';
      html = panel(`<div class="sec"><div class="sec-h">${pages[page][0]}</div><p class="native-note">These commands call the existing engines directly. Close all emulator instances before changing files. Backups and validation stay in C++.</p>${page === 3 ? `<label class="lbl" for="fps-limit">FPS limit</label><input id="fps-limit" class="inp" type="number" min="1" max="10000" data-draft="fps" value="${esc(drafts.fps || '240')}">` : page === 5 ? `<label class="lbl" for="boot-hold">Boot hold (milliseconds)</label><input id="boot-hold" class="inp" type="number" min="0" max="15000" data-draft="hold" value="${esc(drafts.hold || '3000')}">${bootOptions}` : ''}<div class="button-grid">${commands.map(([label, action]) => button(label, action)).join('')}${native(page)}</div><pre id="operation-result"></pre></div>`);
    } else if (page === 8) {
      html = panel(`<div class="sec"><div class="sec-h">Application appearance</div><div class="tools"><select class="inp" id="appearance-mode" aria-label="App theme">${['Dark', 'Light', 'System'].map((v, i) => `<option value="${i}" ${state.appearance === i ? 'selected' : ''}>${v}</option>`).join('')}</select><input type="color" id="accent" aria-label="App accent" value="${esc(state.accent || '#9ce9c1')}">${button('Save appearance', 'appearance', true)}</div></div><div class="sec"><div class="sec-h">Native targets</div>${['exe', 'dll', 'conf'].map(role => `<label class="lbl" for="path-${role}">${{ exe: 'HD-Player.exe', dll: 'Qt5/Qt6 QuickTemplates2 DLL', conf: 'bluestacks.conf' }[role]}</label><div class="path-row"><input class="inp mono" id="path-${role}" value="${esc(state[role + 'Path'] || '')}">${button('Browse…', 'browse', false, `data-role="${role}"`)}${button('Set path', 'path', false, `data-role="${role}"`)}</div>`).join('')}<div class="button-grid">${button('Detect installations', 'detectInstalls')}${button('Restore EXE backup', 'restoreBackup')}</div>${(state.installs || []).map((install, index) => `<div class="kv"><span>${esc(install.profile || install.root)}</span>${button('Use installation', 'useInstall', false, `data-index="${index}"`)}</div>`).join('')}</div>`);
    } else {
      html = panel(editors.render(page));
    }
    $('#main').innerHTML = html;
    if (page !== 0) $('#main .pscroll')?.insertAdjacentHTML('beforeend', editors.extra(page) + (page === 8 ? editors.extra(0) : ''));
    if (page === 2) { renderResources(); renderResourceEditor(); }
    if ($('#operation-result')) $('#operation-result').textContent = operationResult;
    markBusy(); syncVideo();
  }
  function renderResources() {
    if (!$('#resource-rows')) return;
    const rows = BsiResourceSearch.filter(resourceIndex, filter, kind);
    // In All files, put visual assets first so SVGs never appear to be missing
    // just because a large alphabetic block of QML filled the first page.
    if (kind === 0) {
      const order = { svg: 0, image: 1, text: 2, binary: 3 };
      rows.sort((a, b) => (order[a.kind] ?? 4) - (order[b.kind] ?? 4)
        || String(a.path).localeCompare(String(b.path), undefined, { numeric: true, sensitivity: 'base' }));
    }
    offset = Math.max(0, Math.min(offset, Math.max(0, Math.ceil(rows.length / 100) - 1) * 100));
    const svgCount = resources.filter(row => row.kind === 'svg').length;
    const icon = row => {
      const thumb = typeof row.thumbnail === 'string' && /^data:image\/(?:png|jpe?g|webp|gif|bmp|svg\+xml);base64,/i.test(row.thumbnail)
        ? `<img class="resource-thumb" src="${esc(row.thumbnail)}" alt="" loading="lazy" decoding="async">`
        : `<span class="resource-type-mark ${esc(row.kind || 'binary')}" aria-hidden="true">${row.kind === 'svg' ? 'SVG' : row.kind === 'image' ? 'IMG' : row.kind === 'text' ? 'TXT' : 'BIN'}</span>`;
      return `<span class="resource-row-icon">${thumb}</span>`;
    };
    const summary = kind === 0 ? `${rows.length} files · ${svgCount} SVGs · visual assets shown first` : `${rows.length} of ${resources.length} files`;
    $('#resource-rows').innerHTML = `<div class="resource-list-head"><b>Detected resources</b><span>${summary}</span></div><div class="resource-list">${rows.slice(offset, offset + 100).map(r => `<button class="resource-row${r.index === selected?.index ? ' selected' : ''}" data-resource="${r.index}" aria-label="${esc(r.kind)} ${esc(r.path)}" ${enabled() ? '' : 'disabled'}>${icon(r)}<span class="resource-row-name">${esc(r.path)}</span><small>${esc(r.format || r.kind)} · ${esc(r.size)} B</small></button>`).join('')}</div>${rows.length > 100 ? `<div class="tools resource-pager"><button class="btn sm" data-paginate="-1" ${offset ? '' : 'disabled'}>Previous</button><span>${offset + 1}–${Math.min(offset + 100, rows.length)} of ${rows.length}</span><button class="btn sm" data-paginate="1" ${offset + 100 < rows.length ? '' : 'disabled'}>Next</button></div>` : ''}`;
  }
  function renderResourceEditor() {
    if (!$('#resource-editor')) return;
    const box = $('#resource-editor');
    if (!selected || !resourceData) { box.innerHTML = '<div class="empty">Select a scanned resource to inspect it.</div>'; return; }
    const hasPreview = /^data:image\/(?:png|jpe?g|webp|gif|bmp|svg\+xml);base64,/i.test(resourceData.preview || '');
    box.innerHTML = `<div class="resource-editor-head"><div><span class="section-label">Selected resource</span><h3>${esc(selected.path)}</h3></div><span class="pill">${esc(selected.kind)} · ${esc(selected.size)} B</span></div><p class="note" id="resource-budget"></p><div class="resource-detail-grid ${selected.text ? 'has-code' : ''}">${selected.text ? '<label class="resource-source"><span class="editor-field-title">Source editor</span><textarea class="inp" id="resource-code" spellcheck="false" aria-label="Resource source text"></textarea></label>' : ''}<div class="resource-preview-panel"><span class="editor-field-title">Preview</span><img id="resource-image" alt="Resource preview" ${hasPreview ? '' : 'hidden'}><p class="note" id="resource-note"></p></div></div><div class="button-grid">${selected.text ? button('Save resource text', 'saveResource', true) : ''}${button('Replace with file…', 'replaceResource')}${button('Extract…', 'extractResource')}</div>`;
    const budget = resourceData.budget || {};
    $('#resource-budget').textContent = `Decoded: ${selected.size} B · Stored: ${selected.stored} B · Capacity: ${budget.known ? budget.capacity + ' B' : 'unknown'} · ${budget.msg || 'Validated by native engine on save.'}`;
    if ($('#resource-code')) $('#resource-code').value = resourceData.text || '';
    if ($('#resource-image')) {
      if (hasPreview) $('#resource-image').src = resourceData.preview;
      $('#resource-note').textContent = resourceData.note || (hasPreview ? 'Preview generated by the native resource engine.' : 'No safe raster preview is available for this format.');
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
      if (value?.msg) toast(value.msg, /no supported completion gate/i.test(value.msg) ? 'warning' : 'success');
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
    if (!Number.isInteger(index) || index < 0 || index >= pages.length) return;
    if (index === 8) { openSettings('appearance'); return; }
    if (index === page) return;
    if (pending || state.busy || !await confirmDiscard()) return;
    if (dirty) { selected = null; resourceData = null; }
    $('#profile-menu').open = false;
    dirty = false; page = index; operationResult = ''; chrome(); renderView(); BsiEffects.enter($('#main'));
    // Reads only the already-loaded palette. Neither this nor Resources navigation scans files.
    if (page === 1 && enabled()) { await readPalette(); renderView(); }
    await editors.navigate(page);
    if (page === 5 && enabled()) {
      try {
        const logo = await request('bootLogoPreview');
        bootLogoPreview = typeof logo?.logo === 'string' && logo.logo.startsWith('data:image/') ? logo.logo : '';
      } catch (_) { bootLogoPreview = ''; }
      operationResult = '';
    }
    if ([5, 10, 11].includes(page)) renderView();
  }
  async function action(el) {
    const name = el.dataset.action;
    if (!enabled()) return;
    if (name.startsWith('editor:')) { await editors.action(name.slice(7), el, page); renderView(); return; }
    if (name === 'native') { if (await confirmDiscard()) { if (dirty) { selected = null; resourceData = null; } dirty = false; await request('openNativeTool', { page: Number(el.dataset.page) }); } return; }
    if (name === 'loadTheme') { await request('loadTheme', { mode: $('#theme-mode').value }); await readPalette(); }
    else if (name === 'applyTheme') { if (!await BsiEffects.confirm('Apply staged colors to the selected EXE using the native engine?')) return; await request(name); await readPalette(); }
    else if (name === 'preset') { await request('applyPreset', { id: $('#preset').value }); await readPalette(); }
    else if (name === 'scanResources') { if (!await confirmDiscard()) return; dirty = false; selected = null; resourceData = null; filter = ''; kind = 0; offset = 0; if ($('#resource-filter')) $('#resource-filter').value = ''; if ($('#resource-kind')) $('#resource-kind').value = '0'; await request(name); await refreshResources(); }
    else if (name === 'saveResource') { if (!await BsiEffects.confirm('Save this resource into the selected EXE? The native engine will validate storage capacity.')) return; await request(name, { index: selected.index, text: resourceData.text }); dirty = false; await refreshResources(); resourceData = await request('readResource', { index: selected.index }); }
    else if (name === 'replaceResource' || name === 'extractResource') {
      if (name === 'replaceResource' && !await confirmDiscard()) return;
      const path = await transport.chooseFile(name === 'replaceResource' ? 'replace' : 'extract');
      if (!path) return;
      if (name === 'replaceResource' && !await BsiEffects.confirm('Replace the selected EXE resource with this file?')) return;
      await request(name, { index: selected.index, path });
      if (name === 'replaceResource') { dirty = false; await refreshResources(); resourceData = await request('readResource', { index: selected.index }); }
    } else if (name === 'browse') { if (!await confirmDiscard()) return; const path = await transport.chooseFile(el.dataset.role); if (!path) return; const input = $('#path-' + el.dataset.role); if (input) input.value = path; }
    else if (name === 'bootVideoBrowse') { const path = await transport.chooseFile('boot-video'); if (!path) return; drafts.bootVideo = path; }
    else if (name === 'path') { if (!await confirmDiscard()) return; const role = el.dataset.role; await request('setPaths', { [role]: $('#path-' + role).value }); }
    else if (name === 'appearance') await request('setAppearance', { theme: Number($('#appearance-mode').value), accent: $('#accent').value });
    else if (name === 'settings-about-save' || name === 'settings-about-reset') {
      const profile = name === 'settings-about-reset' ? {
        developerName: 'Mr.Pc Gamer & Romulos', tagline: 'BlueStacks tools · with contributor Nova',
        links: [
          {kind:'github',title:'Open-source project',url:'https://github.com/MrPcGamerYT/BSI-Customizer'},
          {kind:'discord',title:'Community Discord',url:'https://discord.gg/5qkKPRZkWa'},
          {kind:'youtube',title:'Mr.Pc Gamer on YouTube',url:'https://www.youtube.com/@MR.PC_GAMER_YT?sub_confirmation=1'},
          {kind:'youtube',title:'Romulos on YouTube',url:'https://www.youtube.com/@Romulus_FF?sub_confirmation=1'}
        ]
      } : {
        developerName: $('#settings-dialog [data-about-field="developerName"]').value,
        tagline: $('#settings-dialog [data-about-field="tagline"]').value,
        links: Array.from(document.querySelectorAll('#settings-dialog [data-about-link]')).map(row => ({
          kind: row.querySelector('[data-about-field="kind"]').value,
          title: row.querySelector('[data-about-field="title"]').value,
          url: row.querySelector('[data-about-field="url"]').value
        }))
      };
      await request('setAboutProfile', profile);
      if (name === 'settings-about-save') toast('About profile saved on this PC.', 'success');
      else toast('Default About profile restored.', 'success');
    }
    else if (name === 'detectInstalls') await request(name);
    else if (name === 'useInstall') { if (!await confirmDiscard()) return; await request(name, { index: Number(el.dataset.index) }); }
    else if (name === 'signOut') { if (await confirmDiscard()) await request(name); return; }
    else {
      const mutating = ['fpsPatch', 'fpsRestore', 'integrityPatch', 'integrityRestore', 'bootPatch', 'purgePlayerCache', 'restoreBackup'].includes(name);
      if (mutating && !await BsiEffects.confirm('Run ' + name + ' on the selected target? Close all emulator instances first.')) return;
      if (name === 'bootPatch') {
        const mode = $('#boot-mode')?.value || drafts.bootMode || 'modern';
        if (mode === 'video' && !drafts.bootVideo) throw new Error('Choose an MP4 before installing the custom boot.');
        await request(name, { holdMs: Number($('#boot-hold').value), mode, sourcePath: mode === 'video' ? drafts.bootVideo : '' });
      } else await request(name, name === 'fpsPatch' ? { limit: Number($('#fps-limit').value) } : {});
    }
    refreshVisibleView();
  }
  $('#settings-close').addEventListener('click', () => $('#settings-dialog').close());
  const settingsHandle = $('#settings-drag-handle');
  const settingsDialog = $('#settings-dialog');
  let settingsDrag = null;
  settingsHandle.addEventListener('pointerdown', event => {
    if (event.button !== 0 || event.target.closest('#settings-close')) return;
    const rect = settingsDialog.getBoundingClientRect();
    settingsDialog.style.left = `${rect.left}px`;
    settingsDialog.style.top = `${rect.top}px`;
    settingsDialog.style.transform = 'none';
    settingsDrag = { id:event.pointerId, x:event.clientX, y:event.clientY, left:rect.left, top:rect.top, width:rect.width, height:rect.height };
    settingsHandle.setPointerCapture(event.pointerId);
  });
  settingsHandle.addEventListener('pointermove', event => {
    if (!settingsDrag || settingsDrag.id !== event.pointerId) return;
    const left = Math.max(8, Math.min(innerWidth - settingsDrag.width - 8, settingsDrag.left + event.clientX - settingsDrag.x));
    const top = Math.max(8, Math.min(innerHeight - settingsDrag.height - 8, settingsDrag.top + event.clientY - settingsDrag.y));
    settingsDialog.style.left = `${left}px`;
    settingsDialog.style.top = `${top}px`;
  });
  for (const eventName of ['pointerup','pointercancel']) settingsHandle.addEventListener(eventName, event => {
    if (settingsDrag?.id === event.pointerId) settingsDrag = null;
  });
  document.addEventListener('click', async event => {
    try {
      const windowButton = event.target.closest('[data-window]');
      if (windowButton) {
        const action = windowButton.dataset.window;
        if (action === 'close' && (!await confirmDiscard() || state.busy || pending)) return;
        if (action !== 'close' || (!state.busy && !pending))
          await BsiEffects.windowAction(action, selected => transport?.windowAction?.(selected));
        return;
      }
      const settingsTabButton = event.target.closest('[data-settings-tab]');
      if (settingsTabButton) { settingsTab = settingsTabButton.dataset.settingsTab; renderSettingsTab(); return; }
      const interfaceTab = event.target.closest('[data-interface-tab]');
      if (interfaceTab) { editors.selectInterfaceTab(interfaceTab.dataset.interfaceTab); renderView(); return; }
      const maintenanceTab = event.target.closest('[data-maint-tab]');
      if (maintenanceTab) { editors.selectMaintenanceTab(maintenanceTab.dataset.maintTab); renderView(); return; }
      const actionEl = event.target.closest('[data-action]');
      if (actionEl) {
        if (actionEl.dataset.action === 'workspaceHome') await navigate(11);
        else if (actionEl.dataset.action.startsWith('editor:')) {
          await editors.action(actionEl.dataset.action.slice(7), actionEl, settingsDialog.open ? 9 : page);
          refreshVisibleView();
        } else await action(actionEl);
        return;
      }
      const nav = event.target.closest('[data-page]');
      if (nav) { await navigate(Number(nav.dataset.page)); return; }
      const row = event.target.closest('[data-resource]');
      if (row && enabled() && await confirmDiscard()) {
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
  $('.title').addEventListener('pointerdown', event => { if (event.button === 0) transport?.windowAction?.('move'); });
  $('.title').addEventListener('dblclick', () => transport?.windowAction?.('max'));
  document.addEventListener('input', event => {
    const el = event.target;
    editors.input(el);
    if (el.matches('[data-about-field]')) updateAboutPreviewFromForm();
    if (page === 6 && /^(about:|link:)/.test(el.dataset.editorField || '')) { const preview = $('#interface-about-preview'); if (preview) preview.innerHTML = editors.aboutPreview(); }
    if (el.id === 'resource-filter') { filter = el.value; offset = 0; renderResources(); }
    if (el.id === 'resource-code' && resourceData) { resourceData.text = el.value; dirty = true; scheduleMeasurement(); }
    if (el.dataset.draft) drafts[el.dataset.draft] = el.value;
  });
  document.addEventListener('change', async event => {
    const el = event.target;
    editors.input(el);
    if (el.matches('[data-about-field]')) updateAboutPreviewFromForm();
    if (el.dataset.draft) drafts[el.dataset.draft] = el.value;
    if (el.id === 'boot-mode') { renderView(); return; }
    if (page === 6 && /^(about:|link:)/.test(el.dataset.editorField || '')) { const preview = $('#interface-about-preview'); if (preview) preview.innerHTML = editors.aboutPreview(); }
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
      transport.onWindowMode?.(updateWindowMode);
      transport.onVideo?.(message => editors.videoStatus(message));
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
