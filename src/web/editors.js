/* Full-tool HTML adapters. All privileged work goes through the authenticated
   native bridge; forms never implement their own EXE-patching logic. */
(() => {
  'use strict';
  window.BsiEditors = {
    create(ctx) {
      const { esc, button, request, chooseFile, getState, getResources } = ctx;
      const maintenance = BsiMaintenance.create(ctx);
      let fields = {}, interfaceInfo = null, profile = {}, operations = [], plan = null;
      let history = [], gradients = [], output = {}, rpcNames = [], instances = [];
      let videoSelected = false, videoStatus = '', interfaceTab = 'layout';
      let imageSource = '', comparison = null, original = null;
      const checked = key => !!fields[key];
      const value = (key, fallback = '') => fields[key] ?? fallback;
      const field = (key, label, fallback = '', type = 'text') => `<label class="editor-field"><span>${esc(label)}</span><input class="inp" data-editor-field="${esc(key)}" type="${type}" value="${esc(value(key, fallback))}" ${type === 'number' ? 'step="1"' : ''}></label>`;
      const area = (key, label, fallback = '') => `<label class="editor-field"><span>${esc(label)}</span><textarea class="inp editor-source" data-editor-field="${esc(key)}">${esc(value(key, fallback))}</textarea></label>`;
      const check = (key, label, fallback = false) => `<label class="editor-check"><input type="checkbox" data-editor-field="${esc(key)}" ${value(key, fallback) ? 'checked' : ''}>${esc(label)}</label>`;
      const btn = (label, action, primary = false, extra = '') => button(label, 'editor:' + action, primary, extra);
      const card = (title, body) => `<div class="sec editor-card"><div class="sec-h">${esc(title)}</div>${body}</div>`;
      const preview = data => /^data:image\/(png|jpeg|webp|gif|bmp);base64,/i.test(data?.preview || '') ? `<img class="editor-preview" src="${esc(data.preview)}" alt="Native resource preview">` : '';
      const result = page => output[page] === undefined ? '' : `<pre class="editor-result">${esc(typeof output[page] === 'string' ? output[page] : JSON.stringify(output[page], (key, val) => key === 'preview' ? '[native preview returned]' : val, 2))}</pre>${preview(output[page])}`;
      const actions = body => `<div class="button-grid">${body}</div>`;
      const confirmWrite = message => BsiEffects.confirm(message + '\nClose emulator instances first. Native validation and backups remain authoritative.');
      const gifFields = [['AppId', 'Discord application ID'], ['State', 'State'], ['LargeImage', 'Large image HTTPS URL / asset key'], ['LargeText', 'Large image tooltip'], ['SmallImage', 'Small image HTTPS URL / asset key'], ['SmallText', 'Small image tooltip'], ['Button1Label', 'Button 1 label'], ['Button1Url', 'Button 1 HTTPS URL'], ['Button2Label', 'Button 2 label'], ['Button2Url', 'Button 2 HTTPS URL']];
      function gifSettings() {
        const settings = Object.fromEntries(gifFields.map(([key]) => [key, String(value('gif:' + key))]));
        return { ...settings, Details: String(value('gif:Details')), Enabled: value('gif:Enabled', true), Timestamp: checked('gif:Timestamp'), Interval: Number(value('gif:Interval', 5)) };
      }
      function aboutSettings() {
        return { text: String(value('about:text', interfaceInfo?.about?.text || '')), showSections: value('about:sections', interfaceInfo?.about?.showSections !== false), links: (interfaceInfo?.about?.links || []).map((link, i) => ({
          ...link, label: String(value('link:' + i + ':label', link.label || '')), url: String(value('link:' + i + ':url', link.url || '')),
          hidden: !value('link:' + i + ':show', !link.hidden)
        })) };
      }
      function aboutPreview() {
        const data = aboutSettings();
        const links = data.links.filter(link => !link.hidden);
        return `<div class="about-live-preview"><div class="about-live-mark">BS</div><span class="section-label">Player About preview</span><h3>${esc(data.text || 'Your player About text')}</h3>${data.showSections && links.length ? `<div class="about-live-links">${links.map(link => `<span>${esc(link.label || link.name || link.id || 'Link')}</span>`).join('')}</div>` : ''}<p class="note">Preview only · changes are written only when you choose Apply About page.</p></div>`;
      }
      function loadProfileFields(next) {
        profile = next || {};
        if (profile.boot) { fields['boot:holdMs'] = profile.boot.holdMs; ctx.setBootHold(profile.boot.holdMs); }
        if (profile.gifRpc) Object.entries(profile.gifRpc).forEach(([key, v]) => { fields['gif:' + key] = v; });
        if (profile.about) {
          fields['about:text'] = profile.about.text || '';
          fields['about:sections'] = profile.about.showSections !== false;
          if (interfaceInfo && profile.about.links) {
            interfaceInfo.about.links = profile.about.links;
            profile.about.links.forEach((link, i) => {
              fields['link:' + i + ':label'] = link.label || '';
              fields['link:' + i + ':url'] = link.url || '';
              fields['link:' + i + ':show'] = !link.hidden;
            });
          }
        }
      }
      async function refreshWorkspace(importFields = false) {
        const data = await request('readWorkspace');
        profile = data?.profile || {}; operations = data?.operations || [];
        if (importFields) loadProfileFields(profile);
      }
      async function workspace(action, options = {}) {
        if (!['inspect', 'history', 'rpc', 'video', 'diagnostics', 'image', 'dry-run'].includes(action)) plan = null;
        const response = await request('workspace', { action, options });
        if (action === 'apply' || action === 'restore') ctx.invalidateResources();
        await refreshWorkspace(action === 'profile-load');
        return response;
      }
      function settingsUpdates() {
        const u = getState().updates || {};
        return card('Release & recovery', `<div class="editor-grid">${[['Installed build', u.currentVersion || getState().version || '—'], ['Updater', u.configured ? 'Configured' : 'Not configured'], ['Status', u.state || 'Idle'], ['Latest release', u.latestVersion || '—'], ['Trust', u.trust || '—'], ['Download size', u.downloadSize || '—']].map(([k, v]) => `<div class="kv"><span>${k}</span><b>${esc(v)}</b></div>`).join('')}</div><p class="note">${esc(u.note || '')}</p>${check('updates:auto', 'Check for updates automatically', u.autoCheck !== false)}${actions(btn('Save preference', 'updates-auto') + btn('Check now', 'update-check', true) + btn('Download verified update', 'update-download') + btn('Apply & restart', 'update-apply') + btn('Cancel download', 'update-cancel') + btn(u.skipped ? 'Unskip release' : 'Skip optional release', u.skipped ? 'update-unskip' : 'update-skip') + btn('Open download page', 'update-openDownloadPage') + btn('Show staged file', 'update-showStagedFile') + btn('Read helper diagnostics', 'update-diagnostics') + btn('Open helper log', 'update-showHelperLog'))}<pre class="editor-result">${esc(u.releaseNotes || '')}</pre>`);
      }
      function render(page) {
        if (page === 12) return maintenance.render() + result(page);
        if (page === 6) {
          const info = interfaceInfo;
          const tabs = [['layout', 'Layout & sidebar'], ['about', 'About page'], ['settings-panel', 'Settings panel rebuild'], ['remove', 'Remove controls']];
          const tabbar = `<div class="editor-tabs" role="tablist" aria-label="Player interface sections">${tabs.map(([id, label]) => `<button class="editor-tab${interfaceTab === id ? ' active' : ''}" role="tab" aria-selected="${interfaceTab === id}" data-interface-tab="${id}">${label}</button>`).join('')}</div>`;
          let section = '<div class="empty">Inspect the selected player UI to load supported controls and About bindings.</div>';
          if (info && interfaceTab === 'layout') section = card('Player layout tweaks', `<p class="note">Apply only tweaks reported as supported for this executable. Each action remains individually reversible.</p>${(info.layout || []).map((t, i) => `<div class="editor-tweak"><b>${esc(t.label || t.id)}</b><p class="note">${esc(t.detail || '')} ${esc(t.state || '')}</p>${t.supported ? actions(btn(t.applied ? 'Restore this tweak' : 'Apply this tweak', 'layout', false, `data-index="${i}"`)) : '<span class="pill">Unsupported by this EXE</span>'}</div>`).join('') || '<p class="note">No layout tweaks reported for this executable.</p>'}`) + maintenance.sidebar();
          if (info && interfaceTab === 'remove') section = card('Remove selected controls', `<p class="note">Select only the unwanted controls; the native engine validates and backs up the executable before writing.</p><div class="editor-grid">${(info.removals || []).map((name, i) => check('remove:' + i, name)).join('') || '<p class="note">No supported removals were reported.</p>'}</div>${actions(btn('Apply selected removals', 'removals', true) + btn('Restore EXE backup', 'restore'))}`);
          if (info && interfaceTab === 'about') section = card('Player About page', `<p class="note">Edit the in-player About text and links. The preview is plain text and does not execute markup or URLs.</p><div class="about-editor-grid"><div>${area('about:text', 'About text', info.about?.text || '')}${check('about:sections', 'Show About/link sections', info.about?.showSections !== false)}${(info.about?.links || []).map((link, i) => `<div class="editor-link">${check('link:' + i + ':show', link.name || link.id || 'Show link', !link.hidden)}${field('link:' + i + ':label', 'Label', link.label || '')}${field('link:' + i + ':url', 'URL', link.url || '')}</div>`).join('')}${actions(btn('Apply About page', 'about-apply', true) + btn('Restore About bindings', 'about-restore') + btn('Save About to profile', 'about-profile'))}</div><div id="interface-about-preview">${aboutPreview()}</div></div>`);
          if (info && interfaceTab === 'settings-panel') {
            const panelState = info.settingsPanel || {};
            const savedProfile = getState().aboutProfile || {};
            const options = getState().settingsPanelProfile || {};
            const tabIcons = options.tabIcons || {};
            const tabs = panelState.tabs || [];
            const availableTabIds = new Set(tabs.map(tab => tab.id));
            const hiddenTabs = new Set((options.hiddenTabs || []).filter(id => availableTabIds.has(id)));
            const savedLinks = Array.from({ length: 4 }, (_, index) => savedProfile.links?.[index] || {
              kind: ['github', 'discord', 'youtube', 'youtube'][index], title: '', url: ''
            });
            const logo = options.logo;
            const logoPreview = logo?.mime && logo?.data
              ? `<img class="settings-custom-logo" src="data:${esc(logo.mime)};base64,${esc(logo.data)}" alt="Selected About logo">`
              : '<div class="settings-custom-logo-fallback">BSI</div>';
            const tabPreview = tabs.slice(0, 9).map((tab, index) => {
              const custom = tabIcons[tab.id];
              const icon = custom?.data ? `<img src="data:image/svg+xml;base64,${esc(custom.data)}" alt="">` : `<i>${['⚙', '▣', '◈', '♫', '⌨', '☷', 'ⓘ'][index % 7]}</i>`;
              return `<span class="settings-preview-tab${index === 0 ? ' active' : ''}">${icon}${esc(tab.label || tab.page)}</span>`;
            }).join('');
            const panelPreview = `<div class="settings-rebuild-preview" role="img" aria-label="Preview: centered Settings contents with editable SVG tab icons"><div class="settings-preview-head"><b>Settings</b><span aria-hidden="true">—　×</span></div><nav class="settings-preview-tabs">${tabPreview}</nav><div class="settings-preview-page"><b>${esc(tabs[0]?.label || 'Settings')}</b><div class="settings-preview-row"><span>Native settings page</span><i>Centered layout</i></div><div class="settings-preview-row"><span>About identity</span><i>${esc(savedProfile.developerName || 'Your name')}</i></div></div></div>`;
            const tabOptions = tabs.map(tab => {
              const icon = tabIcons[tab.id];
              const visibleKey = `settings-panel:visible:${tab.id}`;
              return `<div class="settings-tab-option"><div class="settings-tab-check">${check(visibleKey, '', !hiddenTabs.has(tab.id))}<span><b>${esc(tab.label || tab.page)}</b><small>${esc(tab.page || tab.id)}</small></span></div><div class="settings-tab-art">${icon?.data ? `<img src="data:image/svg+xml;base64,${esc(icon.data)}" alt="Custom icon">` : '<span class="settings-art-empty">Default</span>'}${btn(icon?.data ? 'Replace SVG' : 'Choose SVG', 'settings-panel-icon', false, `data-tab-id="${esc(tab.id)}"`)}${icon?.data ? btn('Clear', 'settings-panel-icon-clear', false, `data-tab-id="${esc(tab.id)}"`) : ''}</div></div>`;
            }).join('');
            const aboutLinks = savedLinks.map((link, index) => `<div class="settings-link-row"><span class="settings-link-index">0${index + 1}</span>${`<label class="editor-field"><span>Service</span><select class="inp" data-editor-field="settings-panel:link:${index}:kind"><option value="github" ${(value(`settings-panel:link:${index}:kind`, link.kind) === 'github') ? 'selected' : ''}>GitHub</option><option value="discord" ${(value(`settings-panel:link:${index}:kind`, link.kind) === 'discord') ? 'selected' : ''}>Discord</option><option value="youtube" ${(value(`settings-panel:link:${index}:kind`, link.kind) === 'youtube') ? 'selected' : ''}>YouTube</option></select></label>`}${field(`settings-panel:link:${index}:title`, 'Link label', link.title)}${field(`settings-panel:link:${index}:url`, 'HTTPS URL', link.url)}</div>`).join('');
            section = card('Rebuild the BlueStacks Settings panel', `<p class="note">Customize the in-player Settings window here: center the native page content, choose which native tabs remain visible, upload an SVG icon for each tab, and edit the About logo, name and links. These choices are saved on this PC; the selected HD-Player.exe changes only when you apply.</p>${panelPreview}<div class="editor-grid"><div class="kv"><span>Patch support</span><b>${panelState.supported ? 'Supported' : 'Not supported'}</b></div><div class="kv"><span>Current panel</span><b>${panelState.applied ? 'Custom rebuild installed' : (Number(panelState.compiledUnitsActive || 0) > 0 && panelState.restorable ? 'Compiled copy still active' : 'Original panel')}</b></div><div class="kv"><span>Native Settings tabs</span><b>${Number(panelState.settingsPageCount || 0)} discovered · ${tabs.length - hiddenTabs.size} visible</b></div><div class="kv"><span>Verified QML dependencies</span><b>${Number(panelState.settingsQmlResourceCount || 0)}</b></div><div class="kv"><span>Embedded compiled QML</span><b>${Number(panelState.compiledUnitsDetected || 0)} found · ${Number(panelState.compiledUnitsActive || 0)} active</b></div><div class="kv"><span>Logo verification</span><b>${panelState.logoFallback ? 'Built-in fallback available' : (panelState.logoStatus ? 'Player logo found' : (panelState.supported ? 'Player logo found' : 'Not yet verified'))}</b></div></div><p class="note">${esc(panelState.detail || 'Inspect the selected player UI to check its embedded QML resources.')}</p>${panelState.qmlResourceSamples?.length ? `<details class="note"><summary>Verified QML resources (${Number(panelState.embeddedQmlResourceCount || 0)} registered)</summary><code>${esc(panelState.qmlResourceSamples.join('\n'))}</code></details>` : ''}${getState().settingsPanelManualTest ? '<p class="note">Manual tester mode: this session targets a disposable HD-Player copy. Apply and Restore cannot change the selected original executable.</p>' : ''}<section class="settings-customization-card"><h3>Native Settings tabs</h3><p class="note">Uncheck a tab to hide it. Custom SVGs are embedded into the rebuilt Settings window; only self-contained SVG files are accepted.</p>${tabs.length ? `<div class="settings-tab-options">${tabOptions}</div>` : '<p class="note">Inspect a supported player to list its live tabs.</p>'}</section><section class="settings-customization-card"><h3>About page identity</h3><p class="note">This is the About page inside the emulator’s rebuilt Settings window.</p><div class="settings-about-identity">${logoPreview}<div><b>About logo</b><p class="note">${esc(logo?.data ? 'A validated custom logo is selected in your local profile; apply the rebuild to write it to the player.' : (panelState.logoStatus || 'Choose a custom logo or use the safe built-in fallback.'))}</p><div class="button-grid">${btn(logo?.data ? 'Replace logo…' : 'Choose logo…', 'settings-panel-logo')}${logo?.data ? btn('Use player/fallback logo', 'settings-panel-logo-clear') : ''}</div></div></div><div class="settings-about-fields">${field('settings-panel:developerName', 'Name under logo', savedProfile.developerName || 'Mr.Pc Gamer & Romulos')}${field('settings-panel:tagline', 'Subtitle', savedProfile.tagline || 'BlueStacks tools · with contributor Nova')}</div><div class="settings-link-list">${aboutLinks}</div></section><div class="button-grid">${btn('Save Settings rebuild options', 'settings-panel-save', true)}${btn('Reset tab visibility & custom artwork', 'settings-panel-reset')}</div>${actions(btn(panelState.applied ? 'Update rebuilt Settings panel' : 'Apply Settings panel rebuild', 'settings-panel-apply', true) + btn('Restore original Settings panel', 'settings-panel-restore', false, panelState.restorable ? '' : 'disabled title="No safe feature-specific recovery snapshot is available"'))}`);

          }
          return card('Player interface', `<p class="note">Inspect once, then choose a focused task. No resources are scanned while navigating.</p>${actions(btn('Inspect player UI', 'interface-load', true) + btn('Export About report', 'about-report'))}${info ? `<p class="note">${esc(info.about?.detail || info.detail || 'Player UI capabilities loaded from the selected executable.')}</p>` : ''}`) + tabbar + section + result(page);
        }
        if (page === 7) return card('Discord application ID', `<p class="note">Uses the existing native offline ID scanner and reversible patcher. No URL fetching or JavaScript binary editing.</p>${actions(btn('Read status', 'rpc-status', true) + btn('Scan application IDs', 'rpc-find') + btn('Load Explorer names', 'rpc-names'))}<div class="editor-grid">${field('rpc:newId', 'New application ID (17–20 digits)')}${field('rpc:oldId', 'Old ID (optional)')}</div>${actions(btn('Dry run ID replacement', 'rpc-dry') + btn('Apply application ID', 'rpc-apply', true) + btn('Undo ID patch', 'rpc-undo'))}`)
          + card('Explorer / Task Manager names', `<label class="editor-field"><span>Version-resource key</span><select class="inp" data-editor-field="rpc:nameKey"><option value="">All supported name keys</option>${rpcNames.map(n => `<option value="${esc(n.key)}" ${value('rpc:nameKey') === n.key ? 'selected' : ''}>${esc(n.key)} · maximum ${esc(n.max)}</option>`).join('')}</select></label>${field('rpc:name', 'New display name')}${actions(btn('Dry run name change', 'rpc-name-dry') + btn('Apply display name', 'rpc-name'))}`) + result(page);
        if (page === 9) {
          const u = getState().updates || {};
          return card('BSI Customizer', '<p class="native-note">Chromium presentation with your original C++ engines. Existing Discord login, access rules, backup checks and signed/pinned update verification remain native.</p><p class="note">Full native editors remain available as a compatibility fallback.</p>')
            + card('Updates & recovery', `<div class="editor-grid">${[['Build', u.currentVersion || getState().version || '—'], ['Updater', u.configured ? 'Configured' : 'Not configured'], ['State', u.state || 'Idle'], ['Latest', u.latestVersion || '—'], ['Trust', u.trust || '—'], ['Download', u.downloadSize || '—']].map(([k, v]) => `<div class="kv"><span>${k}</span><b>${esc(v)}</b></div>`).join('')}</div><p class="note">${esc(u.note || '')}</p>${check('updates:auto', 'Check for updates automatically', u.autoCheck !== false)}${actions(btn('Save auto-check setting', 'updates-auto') + btn('Check now', 'update-check', true) + btn('Download verified update', 'update-download') + btn('Apply & restart', 'update-apply') + btn('Cancel download', 'update-cancel') + btn(u.skipped ? 'Unskip release' : 'Skip optional release', u.skipped ? 'update-unskip' : 'update-skip') + btn('Open download page', 'update-openDownloadPage') + btn('Show staged file', 'update-showStagedFile') + btn('Read helper diagnostics', 'update-diagnostics') + btn('Open helper log', 'update-showHelperLog'))}<pre class="editor-result">${esc(u.releaseNotes || '')}</pre>`)+result(page);
        }
        if (page === 10) return card('Animated Discord presence', `<p class="native-note">The existing RPC-only version.dll is deployed by C++. Discord Desktop must be running. GIF animation and external image acceptance depend on Discord; no live connection is claimed from a saved config.</p>${actions(btn('Inspect DLL / load settings', 'gif-inspect', true) + btn('Load staged profile settings', 'profile-fields') + btn('Check Discord IPC / profile', 'gif-diagnostics'))}<div class="editor-grid">${gifFields.map(([key, label]) => field('gif:' + key, label)).join('')}</div>${area('gif:Details', 'Rotating details — one per line, maximum 10')}${field('gif:Interval', 'Rotation interval (seconds)', 5, 'number')}${check('gif:Enabled', 'Enable presence', true)}${check('gif:Timestamp', 'Show elapsed timestamp')}${check('gif:replaceExisting', 'Explicitly allow replacing an existing proxy (review inspection first)')}${actions(btn('Install / apply native plugin', 'gif-apply', true) + btn('Save settings to profile', 'gif-profile') + btn('Restore proxy / settings', 'gif-restore'))}`)+result(page);
        if (page === 11) {
          const validPlan = plan?.outputSha256 && plan.revision === getState().workspaceRevision && plan.exePath === getState().exePath;
          return card('Compatibility / capacity inspector', `<p class="note">Inspection is explicit. Static mapping verification does not prove runtime compatibility.</p>${actions(btn('Inspect selected EXE', 'ws-inspect', true) + btn('Export diagnostics…', 'ws-diagnostics') + btn('Refresh staged state', 'workspace-read'))}`)
            + card('Portable profiles', `${field('profile:name', 'Profile name', profile.name || 'My workspace')}${actions(btn('Import profile…', 'profile-load') + btn('Export profile…', 'profile-save'))}<pre class="editor-result">${esc(JSON.stringify(profile, null, 2))}</pre>`)
            + card('Batch plan', `<p class="native-note">Queue current QML theme edits or replacements from an explicit Resources scan. Dry Run must succeed before Apply. Fingerprints and recovery journals remain validated by C++.</p>${actions(btn('Queue staged theme', 'ws-queue-theme') + btn('Clear batch', 'ws-clear') + btn('Dry Run', 'ws-dry-run', true) + (validPlan ? btn('Apply verified batch', 'ws-apply', true) : '<button class="btn" disabled title="Run a successful Dry Run for the current plan">Apply verified batch</button>'))}<ol>${operations.map(op => `<li>${esc(op.kind)} — ${esc(op.path || 'staged palette')}</li>`).join('')}</ol><pre class="editor-result">${esc(plan ? JSON.stringify(plan, null, 2) : 'No verified dry run for this target.')}</pre><label class="editor-field"><span>Scanned resource to replace</span><select class="inp" data-editor-field="workspace:resource">${getResources().map(r => `<option value="${r.index}" ${String(value('workspace:resource', '0')) === String(r.index) ? 'selected' : ''}>${esc(r.path)}</option>`).join('')}</select></label>${actions(btn('Choose replacement & queue…', 'ws-queue-resource'))}<p class="note">Scan Resources on the Resources page first. No scan is started here.</p>${comparison ? `<div class="editor-grid"><div><h4>Original</h4>${preview(original)}<pre class="editor-result">${esc((original?.text || '').slice(0, 16384))}</pre></div><div><h4>Staged replacement</h4>${preview(comparison)}<pre class="editor-result">${esc(comparison.text || comparison.msg || 'Replacement queued. Run Dry Run to validate the batch.')}</pre></div></div>` : ''}`)
            + card('Recovery history', `${actions(btn('Load recovery journals', 'ws-history'))}${history.map((entry, i) => `<div class="editor-tweak"><b>${esc(entry.id)}</b><p class="note">${esc(entry.path || entry.source || '')} ${esc(entry.state || '')}</p>${btn('Restore this journal', 'ws-restore', false, `data-index="${i}"`)}</div>`).join('')}`)
            + card('Image preparation', `<p class="note">Creates a new file, never silently replaces a player asset. JPEG alpha loss is refused; animation is not preserved.</p>${actions(btn('Choose source image…', 'image-open'))}<p class="note">${esc(imageSource || 'No source selected')}</p><div class="editor-grid">${field('image:width', 'Width', 512, 'number')}${field('image:height', 'Height', 512, 'number')}${field('image:quality', 'Quality 1–100', 95, 'number')}</div>${check('image:crop', 'Center crop (otherwise fit)')}${actions(btn('Prepare & save new image…', 'image-save'))}`)
            + card('Boot video / RPC diagnostics', `<p class="note">Inspect MP4 metadata with the existing engine. Playback in the actual emulator still requires target testing; no automatic encoder or conversion is claimed.</p>${actions(btn('Inspect MP4…', 'video-inspect') + (videoSelected ? btn('Play / Qt decoder check', 'video-play') + btn('Pause', 'video-pause') + btn('Toggle crop / fit', 'video-crop') : '') + btn('Check saved profile / Discord IPC', 'gif-diagnostics'))}${videoSelected ? '<div id="video-host" style="height:220px;background:#050807;border-radius:12px;overflow:hidden"></div><p id="video-status" class="note">' + esc(videoStatus) + '</p>' : ''}`) + result(page);
        }
        return '';
      }
      function extra(page) {
        if (page === 0) return card('Detected installations & instances', `${actions(btn('Detect installations', 'installs') + btn('Detect instances', 'instances') + btn('Open player folder', 'folder'))}<pre class="editor-result">${esc(instances.length ? JSON.stringify(instances, null, 2) : '')}</pre>`);
        if (page === 1) return card('Advanced theme tools', `${actions(btn('Clear staged edits', 'theme-reset') + btn('Preview legacy edits', 'theme-preview') + btn('Dry run current QML theme', 'theme-dry') + btn('Save theme profile…', 'profile-save') + btn('Queue QML theme in workspace', 'ws-queue-theme') + btn('Recolor preset toggle SVGs', 'theme-toggles'))}${card('Surface gradients', `<p class="note">Load real gradient targets explicitly. Apply or clear solid edits before a gradient; unsupported orientation is reported by the engine.</p>${actions(btn('Load gradient targets', 'gradient-load'))}<label class="editor-field"><span>Surface</span><select class="inp" data-editor-field="gradient:key">${gradients.map(t => `<option value="${esc(t.key)}" ${value('gradient:key') === t.key ? 'selected' : ''}>${esc(t.label || t.key)}${t.supportsOrientation === false ? ' (vertical only)' : ''}</option>`).join('')}</select></label><div class="editor-grid">${field('gradient:first', 'First color', '#7C5CFF', 'color')}${field('gradient:second', 'Second color', '#00C2FF', 'color')}</div>${check('gradient:horizontal', 'Horizontal orientation', true)}${check('gradient:enabled', 'Enable gradient', true)}${actions(btn('Apply native gradient', 'gradient-apply'))}`)}${result(page)}`);
        if (page === 2) return card('Resource tools', `${actions(btn('Extract all scanned resources', 'extract-all'))}<p class="note">Bulk extraction uses the existing native destination convention. Scan Resources explicitly before extracting.</p>`);
        if (page === 3) return card('FPS target detection', `${check('fps:auto', 'Auto-detect QuickTemplates2 DLL', getState().fpsAutoDetect !== false)}${actions(btn('Save auto-detection preference', 'fps-auto'))}`);
        if (page === 5) return card('Boot profile', `<p class="note">Save the boot hold value shown above without deploying it.</p>${actions(btn('Save boot settings to profile', 'boot-profile') + btn('Load staged profile settings', 'profile-fields'))}${result(page)}`);
        return '';
      }
      async function action(name, el, page) {
        let response;
        if (name.startsWith('maint-')) response = await maintenance.action(name.slice(6), el);
        else if (name === 'interface-load') {
          interfaceInfo = await request('readInterface');
          Object.keys(fields).filter(k => /^(about:|link:|remove:)/.test(k)).forEach(k => delete fields[k]);
        } else if (name === 'removals') {
          const names = (interfaceInfo?.removals || []).filter((_, i) => checked('remove:' + i));
          if (!names.length) throw new Error('Select at least one removal.');
          if (!await confirmWrite('Apply the selected player UI removals?')) return;
          response = await request('applyRemovals', { names });
        } else if (name === 'layout') {
          const tweak = interfaceInfo?.layout[Number(el.dataset.index)];
          if (!tweak?.supported) throw new Error('This layout tweak is not supported.');
          if (!await confirmWrite((tweak.applied ? 'Restore ' : 'Apply ') + (tweak.label || tweak.id) + '?')) return;
          response = await request('applyLayoutTweak', { id: tweak.id, enabled: !tweak.applied });
          interfaceInfo = await request('readInterface');
        } else if (name === 'about-apply' || name === 'about-restore') {
          if (!interfaceInfo) throw new Error('Inspect player UI first.');
          if (!await confirmWrite(name === 'about-apply' ? 'Apply the edited About page?' : 'Restore the original About bindings?')) return;
          response = await request('applyAboutPage', { ...aboutSettings(), enabled: name === 'about-apply' });
        } else if (name === 'about-profile') {
          if (!interfaceInfo) throw new Error('Inspect player UI first.');
          response = await workspace('profile-section', { section: 'about', value: aboutSettings() });
        } else if (name === 'settings-panel-icon' || name === 'settings-panel-logo') {
          const kind = name === 'settings-panel-icon' ? 'tab-icon' : 'logo';
          const role = name === 'settings-panel-icon' ? 'settings-panel-svg' : 'settings-panel-logo';
          const path = await ctx.chooseFile(role);
          if (!path) return;
          response = await request('setSettingsPanelAsset', { kind, id: el.dataset.tabId || '', path });
        } else if (name === 'settings-panel-icon-clear' || name === 'settings-panel-logo-clear') {
          const kind = name === 'settings-panel-icon-clear' ? 'tab-icon' : 'logo';
          response = await request('clearSettingsPanelAsset', { kind, id: el.dataset.tabId || '' });
        } else if (name === 'settings-panel-save' || name === 'settings-panel-reset') {
          if (!interfaceInfo) throw new Error('Inspect player UI first.');
          const options = getState().settingsPanelProfile || {};
          const saved = getState().aboutProfile || {};
          const savedLinks = Array.from({ length: 4 }, (_, index) => saved.links?.[index] || {
            kind: ['github', 'discord', 'youtube', 'youtube'][index], title: '', url: ''
          });
          const linkValue = (index, name) => String(value(`settings-panel:link:${index}:${name}`, savedLinks[index][name] || ''));
          if (name === 'settings-panel-reset') {
            if (!await BsiEffects.confirm('Reset visible tabs and remove all custom Settings tab icons and the custom About logo? Your About name and links will be kept.')) return;
            response = await request('saveSettingsPanelProfile', {
              developerName: String(value('settings-panel:developerName', saved.developerName || 'Mr.Pc Gamer & Romulos')),
              tagline: String(value('settings-panel:tagline', saved.tagline || '')),
              links: savedLinks.map((link, index) => ({ kind: linkValue(index, 'kind'), title: linkValue(index, 'title'), url: linkValue(index, 'url') })),
              hiddenTabs: []
            });
            for (const id of Object.keys(options.tabIcons || {}))
              await request('clearSettingsPanelAsset', { kind: 'tab-icon', id });
            if (options.logo?.data) await request('clearSettingsPanelAsset', { kind: 'logo' });
            fields = {};
          } else {
            const tabs = interfaceInfo.settingsPanel?.tabs || [];
            const previousHidden = new Set(options.hiddenTabs || []);
            const hiddenTabs = tabs.filter(tab => !value(`settings-panel:visible:${tab.id}`, !previousHidden.has(tab.id))).map(tab => tab.id);
            response = await request('saveSettingsPanelProfile', {
              developerName: String(value('settings-panel:developerName', saved.developerName || '')),
              tagline: String(value('settings-panel:tagline', saved.tagline || '')),
              links: savedLinks.map((link, index) => ({ kind: linkValue(index, 'kind'), title: linkValue(index, 'title'), url: linkValue(index, 'url') })),
              hiddenTabs
            });
          }
        } else if (name === 'settings-panel-apply' || name === 'settings-panel-restore') {
          if (!interfaceInfo) throw new Error('Inspect player UI first.');
          if (!await confirmWrite(name === 'settings-panel-apply'
            ? 'Apply the centered Settings layout, selected tabs and saved About profile to the chosen BlueStacks player?'
            : 'Restore only the original Settings-window and About-page QML sources?')) return;
          response = await request('applySettingsPanelRebuild', { enabled: name === 'settings-panel-apply' });
          interfaceInfo = await request('readInterface');
          ctx.invalidateResources();
        } else if (name === 'about-report') response = await request('aboutPageReport');
        else if (name === 'restore') { if (!await confirmWrite('Restore the EXE backup?')) return; response = await request('restoreBackup'); ctx.invalidateResources(); interfaceInfo = null; }
        else if (['rpc-status', 'rpc-find', 'rpc-names', 'rpc-undo'].includes(name)) {
          if (name === 'rpc-undo' && !await confirmWrite('Undo the Discord ID patch?')) return;
          response = await request({ 'rpc-status': 'rpcidStatus', 'rpc-find': 'rpcidFind', 'rpc-names': 'rpcidNames', 'rpc-undo': 'rpcidUndo' }[name]);
          if (name === 'rpc-names') rpcNames = response.entries || [];
        } else if (name === 'rpc-dry' || name === 'rpc-apply') {
          const newId = String(value('rpc:newId')).trim();
          if (!/^\d{17,20}$/.test(newId)) throw new Error('Use a 17–20 digit Discord application ID.');
          if (name === 'rpc-apply' && !await confirmWrite('Replace the application ID in the selected EXE?')) return;
          response = await request('rpcidReplace', { newId, oldId: String(value('rpc:oldId')).trim(), dryRun: name === 'rpc-dry' });
        } else if (name === 'rpc-name' || name === 'rpc-name-dry') {
          if (name === 'rpc-name' && !await confirmWrite('Change Explorer / Task Manager display names?')) return;
          response = await request('rpcidSetName', { text: String(value('rpc:name')), key: String(value('rpc:nameKey')), dryRun: name === 'rpc-name-dry' });
        } else if (name === 'gif-inspect') {
          response = await request('gifRpcInspect');
          if (response.installed && response.settings) Object.entries(response.settings).forEach(([key, v]) => { fields['gif:' + key] = v; });
        } else if (name === 'gif-apply') {
          if (!await confirmWrite('Install / update the RPC-only proxy beside the selected player?')) return;
          response = await request('gifRpcApply', { settings: gifSettings(), replaceExisting: checked('gif:replaceExisting') });
        } else if (name === 'gif-restore') { if (!await confirmWrite('Restore the original proxy and RPC settings?')) return; response = await request('gifRpcRestore'); }
        else if (name === 'gif-profile') response = await workspace('profile-section', { section: 'gifRpc', value: gifSettings() });
        else if (name === 'boot-profile') response = await workspace('profile-section', { section: 'boot', value: { holdMs: Number(document.querySelector('#boot-hold')?.value ?? value('boot:holdMs', 3000)) } });
        else if (name === 'profile-fields') { await refreshWorkspace(true); response = 'Loaded staged profile fields. No files were deployed.'; }
        else if (name === 'workspace-read') { await refreshWorkspace(); response = 'Refreshed staged profile and batch. No scan performed.'; }
        else if (name === 'profile-load' || name === 'profile-save') {
          const file = await chooseFile(name === 'profile-load' ? 'profile-open' : 'profile-save'); if (!file) return;
          response = await workspace(name, { file, name: String(value('profile:name', profile.name || 'My workspace')) });
          if (name === 'profile-load') await ctx.refreshPalette();
        } else if (name === 'ws-apply') {
          if (!plan?.outputSha256 || plan.revision !== getState().workspaceRevision || plan.exePath !== getState().exePath) throw new Error('Run Dry Run again for the current target and batch.');
          if (!await confirmWrite('Apply this verified batch? Output SHA-256:\n' + plan.outputSha256)) return;
          const hash = plan.outputSha256; plan = null;
          response = await workspace('apply', { outputSha256: hash });
        } else if (name === 'ws-dry-run') {
          plan = null;
          response = await workspace('dry-run');
          if (response.ok && response.outputSha256) plan = { ...response, revision: getState().workspaceRevision, exePath: getState().exePath };
        } else if (name === 'ws-queue-resource') {
          const row = getResources().find(r => r.index === Number(value('workspace:resource', 0)));
          if (!row) throw new Error('Use Scan Resources first, then select a resource.');
          const file = await chooseFile('replace'); if (!file) return;
          original = await request('readResource', { index: row.index });
          comparison = response = await workspace('queue-resource', { path: row.path, file });
        } else if (name === 'ws-history') { response = await workspace('history'); history = response.entries || []; }
        else if (name === 'ws-restore') {
          const entry = history[Number(el.dataset.index)]; if (!entry?.id) throw new Error('Select a recovery journal.');
          if (!await confirmWrite('Restore recovery journal ' + entry.id + '?')) return;
          response = await workspace('restore', { id: entry.id });
        } else if (name === 'ws-diagnostics') { const file = await chooseFile('report-save'); if (!file) return; response = await workspace('diagnostics', { file }); }
        else if (name.startsWith('ws-')) response = await workspace(name.slice(3));
        else if (name === 'image-open') { imageSource = await chooseFile('image-open') || imageSource; }
        else if (name === 'image-save') {
          if (!imageSource) throw new Error('Choose a source image first.');
          const file = await chooseFile('image-save'); if (!file) return;
          response = await workspace('image', { source: imageSource, file, width: Number(value('image:width', 512)), height: Number(value('image:height', 512)), quality: Number(value('image:quality', 95)), crop: checked('image:crop') });
        } else if (name === 'video-inspect') { const file = await chooseFile('video-open'); if (!file) return; response = await workspace('video', { file }); videoSelected = true; videoStatus = 'Metadata inspected. Press Play to check this app’s Qt decoder; this does not verify the emulator.'; }
        else if (name.startsWith('video-')) { const cmd = name.slice(6); if (cmd === 'crop') fields.videoCrop = !value('videoCrop', true); response = await request('videoControl', {action:cmd, crop:value('videoCrop',true)}); }
        else if (name === 'gif-diagnostics') response = await workspace('rpc');
        else if (name === 'theme-reset') { response = await request('resetThemeEdits'); await ctx.refreshPalette(); }
        else if (name === 'theme-preview') response = await request('previewHex');
        else if (name === 'theme-dry') response = await workspace('theme-dry-run');
        else if (name === 'theme-toggles') {
          const id = document.querySelector('#preset')?.value; if (!id) throw new Error('Load a palette and choose a preset first.');
          if (!await confirmWrite('Recolor the preset toggle SVGs?')) return;
          response = await request('recolorTogglesFromPreset', { id });
        } else if (name === 'gradient-load') { response = await request('loadGradients'); gradients = response.targets || []; }
        else if (name === 'gradient-apply') {
          const key = String(value('gradient:key', gradients[0]?.key || ''));
          if (!key || !gradients.some(t => t.key === key)) throw new Error('Load and choose a real gradient target first.');
          if (!await confirmWrite('Apply this gradient to the selected native surface?')) return;
          response = await request('applyGradient', { key, first: value('gradient:first', '#7C5CFF'), second: value('gradient:second', '#00C2FF'), horizontal: value('gradient:horizontal', true), enabled: value('gradient:enabled', true) });
          await ctx.refreshPalette();
        } else if (name === 'extract-all') { if (!await BsiEffects.confirm('Extract all scanned resources to the native output directory?')) return; response = await request('extractAllResources'); }
        else if (name === 'installs') response = await request('detectInstalls');
        else if (name === 'instances') instances = response = await request('detectInstances');
        else if (name === 'folder') response = await request('openExeFolder');
        else if (name === 'fps-auto') response = await request('setFpsAutoDetect', { enabled: value('fps:auto', getState().fpsAutoDetect !== false) });
        else if (name === 'updates-auto') response = await request('updates', { action: 'autoCheck', enabled: value('updates:auto', getState().updates?.autoCheck !== false) });
        else if (name.startsWith('update-')) {
          const action = name.slice(7);
          if (action === 'apply' && !await BsiEffects.confirm('Install the verified update and restart BSI Customizer?')) return;
          response = await request('updates', { action });
        } else throw new Error('Unknown editor action: ' + name);
        if (response !== undefined) output[page] = response;
      }
      return {
        render, extra, action,
        videoStatus(message) { videoStatus=String(message).slice(0,2048); const node=document.querySelector('#video-status'); if(node)node.textContent=videoStatus; },
        input(el) { maintenance.input(el); if (el.dataset.editorField) fields[el.dataset.editorField] = el.type === 'checkbox' ? el.checked : el.value; },
        selectInterfaceTab(tab) { if (['layout', 'about', 'settings-panel', 'remove'].includes(tab)) interfaceTab = tab; },
        selectMaintenanceTab(tab) { maintenance.selectTab(tab); },
        aboutPreview,
        settingsUpdates,
        reset() { maintenance.reset(); videoSelected=false; videoStatus=''; interfaceTab='layout'; fields = {}; interfaceInfo = null; profile = {}; operations = []; plan = null; history = []; gradients = []; output = {}; rpcNames = []; instances = []; imageSource = ''; comparison = null; original = null; },
        async navigate(page) { if ([5, 10, 11].includes(page) && ctx.enabled()) await refreshWorkspace(); }
      };
    }
  };
})();
