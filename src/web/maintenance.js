/* HTML controls over the original sidebar, Debloat, Android and Root.vhd
   engines. No filesystem or binary-patching implementation is duplicated. */
(() => {
  'use strict';
  window.BsiMaintenance = {
    create({esc, button, request, chooseFile}) {
      let system=null, fields={}, config=null, android=null, sidebar=null, disk=null, entries=[], selected=null, offset=0, section='all', appOffset=0;
      const value=(key, fallback='')=>fields[key] ?? fallback;
      const btn=(label, action, primary=false, extra='')=>button(label,'editor:maint-'+action,primary,extra);
      const field=(key,label,fallback='')=>`<label class="editor-field"><span>${esc(label)}</span><input class="inp" data-editor-field="maint:${esc(key)}" value="${esc(value(key,fallback))}"></label>`;
      const check=(key,label,fallback=false)=>`<label class="editor-check"><input type="checkbox" data-editor-field="maint:${esc(key)}" ${value(key,fallback)?'checked':''}>${esc(label)}</label>`;
      const actions=html=>`<div class="button-grid">${html}</div>`;
      const card=(title,body)=>`<div class="sec editor-card"><div class="sec-h">${title}</div>${body}</div>`;
      const call=(action,args={})=>request('maintenance',{action,...args});
      const confirm=message=>BsiEffects.confirm(message+'\nOriginal native validation, backup and journal rules remain in force.');
      const image=()=>String(value('image')).trim();
      const inner=()=>String(value('directory','/')).trim()||'/';
      async function list(path=inner()) {
        const result=await call('disk-list',{image:image(),innerPath:path});
        fields.directory=result.path||path; disk={...disk,...result}; entries=result.entries||[]; selected=null; offset=0;
        return result;
      }
      function sidebarView() {
        return card('Sidebar button order', `<p class="note">Uses the original titlebar/sidebar layout engine. Inspect the applied layout before staging an order.</p>${actions(btn('Inspect sidebar row','sidebar-read'))}${sidebar ? `<p class="note">${esc(sidebar.detail||'')}</p>${sidebar.supported&&sidebar.applied?`<ol class="sidebar-order">${(sidebar.buttons||[]).map((row,index)=>`<li>${check('hide:'+row.key,row.label||row.key,row.hidden)}<span>Hide</span>${btn('↑','sidebar-up',false,`data-index="${index}"`)}${btn('↓','sidebar-down',false,`data-index="${index}"`)}</li>`).join('')}</ol>${actions(btn('Apply staged order','sidebar-apply',true)+btn('Reset original order','sidebar-reset'))}`:''}`:''}`);
      }
      function render() {
        const installed=(android?.apps||[]).filter(a=>a.installed);
        const needle=String(value('packageFilter')).toLowerCase();
        const filtered=(android?.apps||[]).filter(a=>(a.package+' '+a.label+' '+(a.note||'')).toLowerCase().includes(needle));
        const chosen=installed.filter(a=>a.debloatable&&value('package:'+a.package,false)).length;
        const tabs=`<div class="maintenance-tabs" role="group" aria-label="Maintenance tools">${[['all','All tools'],['system','System & diagnostics'],['optimizer','Configuration optimizer'],['adb','ADB apps & storage'],['disk','Deep Engine Image Debloater']].map(([key,label])=>btn(label,'section',false,`data-section="${key}" aria-pressed="${section===key}"`)).join('')}</div>`;
        const common=card('Debloat & Android', `<p class="native-note">Original tools, restored in detail. Configuration changes need a stopped player; live ADB cleanup needs the selected instance running. No placebo RAM boost or arbitrary shell commands.</p>${field('instance','Instance name (existing bluestacks.conf instance)')}${config?.state?.instances?.length?`<p class="note">Known instances: ${esc(config.state.instances.join(', '))}</p>`:''}${actions(btn('Inspect configuration','debloat-read',true)+btn('Read Android undo state','android-read'))}`);
        const gb=n=>Number.isFinite(Number(n))&&Number(n)>=0?Number(n).toFixed(1)+' GB':'Not available';
        const systemView=card('System & emulator readiness',`<p class="native-note">Read-only hardware and disk snapshot; no RAM cleaner, service disabling or hidden power-plan changes. Refresh is manual.</p>${actions(btn('Read system & ADB status','system-read',true))}${system?`<p class="note">${esc(system.os)} · Snapshot, not a live monitor.</p><div class="optimizer-facts">${[['Logical CPUs',system.cores],['Total RAM',gb(system.ramGb)],['Available RAM',gb(system.availableRamGb)],['System disk free',gb(system.systemFreeGb)],['Player drive free',gb(system.targetFreeGb)]].map(([k,v])=>`<div><small>${esc(k)}</small><b>${esc(v)}</b></div>`).join('')}</div><p class="note">Resolved ADB executable (not launched by this check):</p><code class="optimizer-path">${esc(system.adbPath||'Not found — put trusted ADB sidecars beside the customizer or use the player bundled HD-Adb.exe.')}</code><p class="note optimizer-path">Customizer search folders: ${esc((system.adbDirectories||[]).join(' · '))}. Current directory and PATH are never searched.</p>`:''}<p class="note">Open a Windows settings panel to choose policy yourself. Nothing is changed by opening it. Power modes can trade battery life / heat for throughput; hardware and drivers determine any benefit.</p>${actions(btn('Power settings…','system-power')+btn('Graphics preferences…','system-graphics')+btn('Game Mode settings…','system-gaming')+btn('Storage settings…','system-storage'))}<p class="note">For emulator changes below: close the player, choose an existing instance, inspect configuration, stage a profile and apply explicitly. Cache deletion is irreversible and is not a guaranteed FPS boost; the existing Boot / Cache page retains that tool.</p>`);
        const configView=card('Configuration optimizer & debloater', `${actions(btn('Stage 60 FPS balanced','preset-balanced')+btn('Stage 30 FPS efficient','preset-efficient')+btn('Stage 240 FPS mode','preset-performance')+btn('Stage quiet / no Smart Downloads','preset-quiet'))}<p class="note">Profiles replace the checkbox selection only; they do not apply changes. Choose only one frame-rate profile. 30/60 FPS caps may reduce rendering load but sacrifice peak FPS; all three frame settings must be supported by that instance.</p>${config?`<p class="note">${esc(config.state?.detail||'')}</p>${[...new Set((config.options||[]).map(o=>o.group||'other'))].map(group=>`<h4 class="option-group">${esc({ads:'Ads & telemetry',tuning:'Performance & ADB access',cloud:'Cloud integrations',system:'Windows integration'}[group]||group)}</h4><div class="editor-grid">${config.options.filter(o=>(o.group||'other')===group).map(o=>`<div>${check('option:'+o.id,o.label)}<p class="option-hint">${esc(o.hint||'')}${o.needsInstance?' · Selected instance required':''}${o.windowsOnly?' · Windows only':''}${config.state?.already?.includes(o.id)?' · Already applied':''}</p></div>`).join('')}</div>`).join('')}`:'<p class="note">Inspect configuration explicitly to load the original supported options: ads, telemetry, downloads, cloud, quiet notifications, high-FPS mode, ADB access, dedicated GPU and supported Windows cleanup. Missing config keys are never invented.</p>'}${actions(btn('Select all configuration options','options-all')+btn('Clear configuration selection','options-clear')+btn('Apply selected cleanup','debloat-apply')+btn('Restore recorded cleanup','debloat-restore'))}<p class="note">High-FPS tuning uses the player's own settings, not the binary FPS patcher. GPU preference is a Windows setting, not a guaranteed speed improvement. Apply/restore keeps the original native records.</p>`);
        const adbView=card('Live ADB · installed packages', `<p class="native-note">Uses trusted app-local adb.exe (beside the customizer or in adb/), falling back to the player's HD-Adb.exe, against a running instance. All installed packages are visible, including unreviewed ones. Removing Android services or game dependencies can break the instance.</p>${actions(btn('Scan running instance','android-scan',true)+btn('Select installed packages','packages-all')+btn('Clear package selection','packages-clear'))}${field('packageFilter','Search package ID, label or description')}${actions(btn('Filter loaded packages','package-filter'))}<p class="maintenance-summary">${installed.length} installed · ${chosen} selected · ${filtered.length} matching. Reviewed labels are guidance, not a guarantee of safety.</p>${filtered.slice(appOffset,appOffset+100).map(a=>`<div class="package-row">${a.installed&&a.debloatable?check('package:'+a.package,a.label||a.package):`<b>${esc(a.label||a.package)}</b>`}<div class="package-meta"><span>${a.installed?'Installed':'Not installed / recorded'}</span><span>${a.reviewed?'Reviewed rule':'Unreviewed package'}</span>${a.recommended?'<span>Recommended by original rule</span>':''}</div><code>${esc(a.package)}</code><p class="note">${esc(a.note||'')}${a.recorded?' · Native restore record present':''}</p></div>`).join('')||'<p class="note">No package results loaded. Click Scan running instance.</p>'}${actions((appOffset?btn('Previous packages','packages-prev'):'')+(appOffset+100<filtered.length?btn('Next packages','packages-next'):''))}`)
          +card('Guest storage cleanup & undo',`<p class="note">Download folder: ${esc(android?.download?.size||'not measured')} · ${android?.download?.entries?.length||0} listed item(s). Storage deletion is not a generic FPS optimization.</p>${check('wipeDownloads','Clear guest Downloads')}${check('keepCopy','Keep a local copy before clearing guest storage',true)}${check('clearAppData','Clear guest Android/data and obb — destructive; game data can be lost')}<p class="lab-warning">The local-copy option covers Downloads, not Android/data or obb. An undo record cannot recreate deleted game data. Back up what you need first.</p>${actions(btn('Apply reviewed selection','android-apply')+btn('Restore recorded Android changes','android-restore'))}${android?.removed?.length?`<p class="note">${android.removed.length} recorded removal(s) available for restore.</p>`:''}`);
        const diskView=card('Root.vhd / engine image', `<p class="note">Close BlueStacks before editing an image. The original engine decides writable/read-only state and performs journalled changes.</p>${field('image','Image or engine archive path')}${actions(btn('Find image','disk-find')+btn('Browse image…','disk-browse')+btn('Open engine archive…','disk-archive')+btn('Inspect image','disk-info',true)+btn('Enable write access','disk-enable'))}${disk?`<p class="note">${esc(disk.description||disk.msg||disk.reason||'')} · ${disk.writable?'Engine reports writable':'Read-only / not enabled'}</p>`:''}${field('directory','Folder in image','/')}${actions(btn('List folder','disk-list')+btn('Parent folder','disk-parent'))}${field('query','Whole-image search (explicit)')}${actions(btn('Search image','disk-search'))}<div class="disk-entries">${entries.slice(offset,offset+200).map((entry,localIndex)=>`<button class="btn${selected===offset+localIndex?' selected':''}" data-action="editor:maint-disk-select" data-index="${offset+localIndex}" title="${esc(entry.path)}">${entry.dir?'▸':'·'} ${esc(entry.name||entry.path)} <small>${esc(entry.size||0)} B</small></button>`).join('')}</div><p class="note">${entries.length ? offset+1 : 0}–${Math.min(entries.length,offset+200)} / ${entries.length}</p>${actions((offset?btn('Previous','disk-previous'):'')+(offset+200<entries.length?btn('Next','disk-next'):''))}<p class="note">Selected: ${esc(selected===null?'none':entries[selected]?.path||'none')}</p>${actions(btn('Open selected folder','disk-open')+btn('Extract selected…','disk-extract')+btn('Add file…','disk-add')+btn('Add folder tree…','disk-add-folder')+btn('Replace selected…','disk-replace'))}<div class="editor-grid">${field('name','New folder / rename / copy name')}${field('destination','Copy / move destination in image','/')}</div>${actions(btn('Create folder','disk-mkdir')+btn('Rename selected','disk-rename')+btn('Copy selected','disk-copy')+btn('Move selected','disk-move')+btn('Delete selected','disk-remove')+btn('Undo last image change','disk-undo')+btn('Discard undo journal','disk-discard'))}`);
        return common+tabs+(section==='all'||section==='system'?systemView:'')+(section==='all'||section==='optimizer'?configView:'')+(section==='all'||section==='adb'?adbView:'')+(section==='all'||section==='disk'?diskView:'');
      }
      async function action(name,el) {
        if(name==='system-read'){system=await call(name);return system;}
        if(['system-power','system-storage','system-graphics','system-gaming'].includes(name))return call('system-settings',{setting:name.slice(7)});
        if(name.startsWith('preset-')){
          if(!config)throw Error('Inspect configuration first, then stage a profile.');
          const presets={'preset-balanced':['balancedfps','downloads'],'preset-efficient':['efficientfps','downloads','quiet'],'preset-performance':['highfps'],'preset-quiet':['quiet','downloads']};
          const ids=presets[name];if(!ids)throw Error('Unknown staged profile.');
          for(const option of config.options||[])fields['option:'+option.id]=ids.includes(option.id);
          return 'Profile staged only. Review the selected instance and options, then Apply selected cleanup. Restore recorded cleanup reverts recorded values.';
        }
        if(name==='section'){section=el.dataset.section;return;}
        if(name==='package-filter'){appOffset=0;return;}
        if(name==='packages-prev'){appOffset=Math.max(0,appOffset-100);return;}
        if(name==='packages-next'){appOffset+=100;return;}
        if(name==='packages-all'||name==='packages-clear'){
          for(const app of android?.apps||[])if(app.installed&&app.debloatable)fields['package:'+app.package]=name==='packages-all';
          return 'Package selection staged only. Review before applying.';
        }
        if(name==='options-all'||name==='options-clear'){
          for(const option of config?.options||[])fields['option:'+option.id]=name==='options-all'&&!['balancedfps','efficientfps'].includes(option.id);
          return 'Configuration selection staged only. Apply explicitly.';
        }

        if(name==='sidebar-read') { sidebar=await call(name); return sidebar; }
        if(name==='sidebar-up'||name==='sidebar-down') {
          const index=Number(el.dataset.index), to=index+(name==='sidebar-up'?-1:1);
          if(sidebar&&to>=0&&to<sidebar.buttons.length) [sidebar.buttons[index],sidebar.buttons[to]]=[sidebar.buttons[to],sidebar.buttons[index]];
          return 'Sidebar order staged. Apply explicitly to write.';
        }
        if(name==='sidebar-apply'||name==='sidebar-reset') {
          if(!sidebar) throw Error('Inspect sidebar row first.');
          if(!await confirm('Apply sidebar ordering to the selected player?')) return;
          const rows=name==='sidebar-reset'?[]:sidebar.buttons||[];
          const result=await call('sidebar-apply',{order:rows.map(r=>r.key),hidden:rows.filter(r=>value('hide:'+r.key,r.hidden)).map(r=>r.key)});
          sidebar=await call('sidebar-read'); return result;
        }
        if(name==='debloat-read') { config=await call(name); return config; }
        if(name==='android-read') { android=await call(name); return android; }
        if(name==='android-scan') { Object.keys(fields).filter(k=>k.startsWith('package:')).forEach(k=>delete fields[k]); appOffset=0; android=await call(name,{instance:String(value('instance'))}); return android; }
        if(name==='debloat-apply') {
          const ids=(config?.options||[]).filter(o=>value('option:'+o.id,false)).map(o=>o.id);
          if(!ids.length) throw Error('Inspect configuration and select at least one cleanup option.');
          if(!await confirm('Apply the selected configuration cleanup?')) return;
          const result=await call(name,{ids,instance:String(value('instance'))}); config=await call('debloat-read'); return result;
        }
        if(name==='debloat-restore'||name==='android-restore') {
          if(!await confirm('Restore the recorded changes? Start the relevant instance if Android restore is needed.')) return;
          return call(name,{instance:String(value('instance'))});
        }
        if(name==='android-apply') {
          const packages=(android?.apps||[]).filter(a=>a.installed&&a.debloatable&&value('package:'+a.package,false)).map(a=>a.package);
          if(!packages.length&&!value('wipeDownloads',false)&&!value('clearAppData',false)) throw Error('Scan and review a package or storage selection first.');
          if(!await confirm('Remove selected packages / clear selected guest folders? Unreviewed removals can break the instance.')) return;
          return call(name,{packages,instance:String(value('instance')),wipeDownloads:!!value('wipeDownloads',false),keepCopy:!!value('keepCopy',true),clearAppData:!!value('clearAppData',false)});
        }
        if(name==='disk-find') { const result=await call(name); fields.image=result.path||''; disk=null; entries=[]; selected=null; offset=0; return result; }
        if(name==='disk-browse'||name==='disk-archive') {
          const file=await chooseFile('disk-open'); if(!file)return;
          fields.image=file; disk=null; entries=[]; selected=null; offset=0;
          if(name==='disk-archive') { const result=await call(name,{file}); fields.image=result.path||file; return result; }
          return 'Image path selected. Inspect before modifying.';
        }
        if(name==='disk-previous') {offset=Math.max(0,offset-200);return;}
        if(name==='disk-next') {offset=Math.min(Math.floor((entries.length-1)/200)*200,offset+200);return;}
        if(name==='disk-select') { selected=Number(el.dataset.index); return 'Selected '+(entries[selected]?.path||''); }
        if(!image()) throw Error('Choose an image first.');
        if(name==='disk-info') { disk=await call(name,{image:image()}); return disk; }
        if(name==='disk-enable') {
          if(!await confirm('Enable write access for this closed engine image? Dirty filesystems remain refused by the engine.')) return;
          disk=await call(name,{image:image()}); return disk;
        }
        if(name==='disk-list') return list();
        if(name==='disk-parent') return list(inner().split('/').slice(0,-1).join('/')||'/');
        if(name==='disk-search') { const result=await call(name,{image:image(),query:String(value('query'))}); entries=result.entries||[]; selected=null; offset=0; return result; }
        const entry=selected===null?null:entries[selected];
        if(name==='disk-open') { if(!entry?.dir)throw Error('Select a folder.'); return list(entry.path); }
        if(name==='disk-extract') {
          if(!entry)throw Error('Select an entry.');
          const file=await chooseFile(entry.dir?'disk-folder':'disk-extract'); if(!file)return;
          return call(name,{image:image(),innerPath:entry.path,file});
        }
        if(!disk?.writable) throw Error('Inspect the image and enable native write access first.');
        const entryActions=['disk-replace','disk-rename','disk-copy','disk-move','disk-remove'];
        if(entryActions.includes(name)&&!entry)throw Error('Select an entry.');
        let file='';
        if(['disk-add','disk-add-folder','disk-replace'].includes(name)) { file=await chooseFile(name==='disk-add-folder'?'disk-folder':'disk-source'); if(!file)return; }
        if(!await confirm('Run '+name+' on this image? Verify the image path and selection before continuing.'))return;
        const result=await call(name,{image:image(),innerPath:entryActions.includes(name)?entry.path:inner(),file,name:String(value('name')),destination:String(value('destination','/'))});
        await list(); return result;
      }
      return {render,sidebar:sidebarView,action,
        input(el) { if(el.dataset.editorField?.startsWith('maint:')) { const key=el.dataset.editorField.slice(6); fields[key]=el.type==='checkbox'?el.checked:el.value; if(el.checked&&['option:highfps','option:balancedfps','option:efficientfps'].includes(key)){for(const id of ['highfps','balancedfps','efficientfps']){const selected=key==='option:'+id;fields['option:'+id]=selected;const peer=document.querySelector('[data-editor-field="maint:option:'+id+'"]');if(peer)peer.checked=selected;}} if(key==='image') {disk=null;entries=[];selected=null;offset=0;} } },
        reset() {system=null;section='all';appOffset=0;fields={};config=null;android=null;sidebar=null;disk=null;entries=[];selected=null;offset=0;}
      };
    }
  };
})();
