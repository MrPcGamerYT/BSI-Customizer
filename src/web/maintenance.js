/* HTML controls over the original sidebar, Debloat, Android and Root.vhd
   engines. No filesystem or binary-patching implementation is duplicated. */
(() => {
  'use strict';
  window.BsiMaintenance = {
    create({esc, button, request, chooseFile}) {
      let fields={}, config=null, android=null, sidebar=null, disk=null, entries=[], selected=null, offset=0, mode='config';
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
        const writable = !!disk?.writable;
        const locked = 'disabled title="Inspect the image and enable native write access first"';
        const crumbs = (() => {
          const parts = String(value('directory', '/')).split('/').filter(Boolean);
          const rows = [{label:'/',path:'/'}]; let path='';
          for (const part of parts) { path += '/' + part; rows.push({label:part,path}); }
          return rows;
        })();
        const folders = entries.filter(entry=>entry.dir).slice(0,100);
        const shown = entries.slice(offset,offset+200);
        const selectedEntry = selected===null ? null : entries[selected];
        const tabs = [['config','Configuration'],['android','Android / ADB'],['disk','Root image file manager']];
        const tabbar = `<div class="editor-tabs" role="tablist" aria-label="Debloat and disk tools">${tabs.map(([id,label])=>`<button class="editor-tab${mode===id?' active':''}" role="tab" aria-selected="${mode===id}" data-maint-tab="${id}">${label}</button>`).join('')}</div>`;
        const header = card('Debloat & Android', `<p class="note">Choose one focused task. Reads and scans are explicit; installed packages are listed for review, and image edits are journalled by the native engine.</p>${field('instance','Instance name (existing bluestacks.conf instance)')}`);
        const configPanel = card('Configuration cleanup', `${actions(btn('Inspect supported options','debloat-read',true))}${config?`<p class="note">${esc(config.state?.detail||'')}</p><div class="editor-grid">${(config.options||[]).map(o=>`<div>${check('option:'+o.id,o.label)}<p class="note">${esc(o.hint||'')}${o.needsInstance?' · requires instance name':''}</p></div>`).join('')}</div>`:'<p class="note">Inspect configuration to load the original supported option list.</p>'}${actions(btn('Apply selected cleanup','debloat-apply')+btn('Restore recorded cleanup','debloat-restore'))}`);
        const androidPanel = card('Live Android / ADB', `<p class="note">Run the selected instance before scanning. The native scan lists installed packages; nothing is selected automatically.</p>${actions(btn('Read Android undo state','android-read')+btn('Scan running instance','android-scan',true))}${android?.apps?`<div class="editor-grid">${android.apps.filter(a=>a.installed&&a.debloatable).map(a=>`<div>${check('package:'+a.package,a.label||a.package)}<p class="note">${esc(a.package)} · ${esc(a.note||'')}</p></div>`).join('')}</div><p class="note">Downloads: ${esc(android.download?.size||'not measured')}</p>`:''}${check('wipeDownloads','Clear guest Downloads')}${check('keepCopy','Keep a local copy before clearing guest storage',true)}${check('clearAppData','Clear guest Android/data and obb — review before enabling')}${actions(btn('Apply reviewed selection','android-apply')+btn('Restore recorded Android changes','android-restore'))}`);
        const diskPanel = card('Root image file manager', `<p class="note">Close BlueStacks before editing. The native engine decides writable/read-only state, preserves dirty ext4 images, and journals edits for recovery.</p><div class="disk-open-controls">${field('image','VHD / VHDX or engine archive path')}${actions(btn('Find image','disk-find')+btn('Browse…','disk-browse')+btn('Open archive…','disk-archive')+btn('Inspect / open image','disk-info',true)+btn('Enable write access','disk-enable',false,writable?'disabled title="Image is already writable"':''))}</div>${disk?`<div class="disk-status ${writable?'is-writable':'is-readonly'}"><span class="disk-status-dot"></span><div><b>${esc(writable?'Writable image':'Read-only image')}</b><p>${esc(disk.description||disk.msg||disk.reason||'Native engine status loaded.')}</p></div><span class="pill">${esc(disk.format||'VHD / VHDX')}</span></div>`:''}${disk?.ok?`<div class="disk-manager"><aside class="disk-tree"><div class="disk-pane-title">Folders</div>${crumbs.map(crumb=>`<button class="disk-tree-item${crumb.path===inner()?' active':''}" data-action="editor:maint-disk-go-path" data-path="${esc(crumb.path)}">${crumb.path==='/'?'▣':'↳'} ${esc(crumb.label)}</button>`).join('')}<div class="disk-tree-sep"></div>${folders.map(entry=>`<button class="disk-tree-item" data-action="editor:maint-disk-open-at" data-index="${entries.indexOf(entry)}">▸ ${esc(entry.name||entry.path)}</button>`).join('')||'<p class="note">No child folders in this view.</p>'}</aside><section class="disk-file-pane"><div class="disk-toolbar"><div class="disk-breadcrumbs">${crumbs.map((crumb,i)=>`${i?'<span>/</span>':''}<button data-action="editor:maint-disk-go-path" data-path="${esc(crumb.path)}">${esc(crumb.label)}</button>`).join('')}</div><div class="tools">${btn('Up','disk-parent')}${btn('Refresh','disk-list')}</div></div><div class="disk-toolbar"><label class="disk-search">Search image <input class="inp" data-editor-field="maint:query" value="${esc(value('query'))}" placeholder="Search paths…"></label>${btn('Search','disk-search')}${btn('Add file…','disk-add',false,writable?'':locked)}${btn('Add folder tree…','disk-add-folder',false,writable?'':locked)}</div><div class="disk-file-head"><span>Name</span><span>Type</span><span>Size</span><span></span></div><div class="disk-file-list">${shown.map((entry,localIndex)=>{const index=offset+localIndex;return `<div class="disk-file-row${selected===index?' selected':''}"><button class="disk-file-select" data-action="editor:maint-disk-select" data-index="${index}" title="Select ${esc(entry.path)}"><span class="disk-file-icon">${entry.dir?'▰':'▤'}</span><span class="disk-file-name">${esc(entry.name||entry.path)}</span></button><span class="disk-file-type">${entry.dir?'Folder':'File'}</span><span class="disk-file-size">${esc(entry.size||0)} B</span>${entry.dir?btn('Open','disk-open-at',false,`data-index="${index}"`):'<span></span>'}</div>`;}).join('')||'<div class="empty">No entries in this folder. Use Refresh or choose another folder.</div>'}</div><div class="disk-file-foot"><span>${entries.length?`${offset+1}–${Math.min(entries.length,offset+200)} of ${entries.length}`:'No entries'}</span><div class="tools">${offset?btn('Previous','disk-previous'):''}${offset+200<entries.length?btn('Next','disk-next'):''}</div></div></section><aside class="disk-details"><div class="disk-pane-title">Selection</div>${selectedEntry?`<div class="disk-selected-icon">${selectedEntry.dir?'▰':'▤'}</div><b class="disk-selected-name">${esc(selectedEntry.name||selectedEntry.path)}</b><p class="note">${esc(selectedEntry.path)}</p><div class="disk-detail-kv"><span>Type</span><b>${selectedEntry.dir?'Folder':'File'}</b><span>Size</span><b>${esc(selectedEntry.size||0)} B</b></div>${actions(btn(selectedEntry.dir?'Open folder':'Extract…',selectedEntry.dir?'disk-open':'disk-extract')+(!selectedEntry.dir?btn('Replace…','disk-replace',false,writable?'':locked):''))}<div class="editor-grid">${field('name','Rename / copy name')}${field('destination','Copy / move destination','/')}</div>${actions(btn('Rename','disk-rename',false,writable?'':locked)+btn('Copy','disk-copy',false,writable?'':locked)+btn('Move','disk-move',false,writable?'':locked)+btn('Delete','disk-remove',false,writable?'':locked))}`:'<p class="note">Select a file or folder to see details and available actions.</p>'}</aside></div><div class="disk-footer-actions">${actions(btn('Create folder','disk-mkdir',false,writable?'':locked)+btn('Undo last image change','disk-undo',false,writable?'':locked)+btn('Discard undo journal','disk-discard',false,writable?'':locked))}</div>`:'<div class="disk-manager-empty"><span class="disk-empty-icon">▣</span><b>Open a disk image to browse its files</b><p>Choose Find image or Browse, then Inspect / open image. VHDX is supported when the native engine accepts its metadata.</p></div>'}`);
        return header + tabbar + (mode==='config' ? configPanel : mode==='android' ? androidPanel : diskPanel);

      }
      async function action(name,el) {
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
        if(name==='android-scan') { android=await call(name,{instance:String(value('instance'))}); return android; }
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
        if(name==='disk-info') { disk=await call(name,{image:image()}); if(disk?.ok) await list('/'); return disk; }
        if(name==='disk-enable') {
          if(!await confirm('Enable write access for this closed engine image? Dirty filesystems remain refused by the engine.')) return;
          disk=await call(name,{image:image()}); if(disk?.ok) await list(inner()); return disk;
        }
        if(name==='disk-list') return list();
        if(name==='disk-parent') return list(inner().split('/').slice(0,-1).join('/')||'/');
        if(name==='disk-go-path') return list(String(el.dataset.path||'/'));
        if(name==='disk-search') { const result=await call(name,{image:image(),query:String(value('query'))}); entries=result.entries||[]; selected=null; offset=0; return result; }
        const entry=selected===null?null:entries[selected];
        if(name==='disk-open-at') { const row=entries[Number(el.dataset.index)]; if(!row?.dir) throw Error('Select a folder.'); selected=Number(el.dataset.index); return list(row.path); }
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
      return {render,sidebar:sidebarView,action,selectTab(tab) { if(['config','android','disk'].includes(tab)) mode=tab; },
        input(el) { if(el.dataset.editorField?.startsWith('maint:')) { const key=el.dataset.editorField.slice(6); fields[key]=el.type==='checkbox'?el.checked:el.value; if(key==='image') {disk=null;entries=[];selected=null;offset=0;} } },
        reset() {fields={};config=null;android=null;sidebar=null;disk=null;entries=[];selected=null;offset=0;mode='config';}
      };
    }
  };
})();
