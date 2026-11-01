/* Read-only native PE inspection. Offsets are data, never executable JS. */
(() => {
  'use strict';
  window.BsiBinaryLab={create({esc,button,request,chooseFile,getState}) {
    let report=null, path='', tab='sections', offset=0, query='', hexOffset='0', hexText='', stringRows=[], scanNote='', message='';
    const btn=(label,action,extra='')=>button(label,'editor:lab-'+action,false,extra);
    const hx=n=>Number(n)<0?'unmapped':'0x'+Number(n).toString(16).toUpperCase();
    const tabs=['sections','imports','exports','resources','strings','hex'];
    const card=(title,body)=>`<div class="sec editor-card"><div class="sec-h">${esc(title)}</div>${body}</div>`;
    const field=(key,label,value)=>`<label class="editor-field"><span>${label}</span><input class="inp mono" data-editor-field="lab:${key}" value="${esc(value)}"></label>`;
    const commands=body=>`<div class="button-grid">${body}</div>`;
    function rows(){return tab==='strings'?stringRows:report?.[tab]||[];}
    function render() {
      let body=card('Binary Lab · deep EXE / DLL inspection',`<p class="native-note">Inspect PE headers, section layout, imported DLLs, named exports and Win32 resources. Separate from Qt Resource explorer; nothing scans on page entry. This tool never patches or executes the source file.</p>${field('path','Source EXE / DLL (blank uses selected player)',path)}${commands(btn('Browse binary…','browse')+btn('Inspect binary','inspect')+btn('Use active player','target'))}<p class="note">${esc(message||'Explicit inspection · maximum 256 MiB · no overwrite exports')}</p>`);
      if(!report)return body;
      body+=`<div class="lab-metrics">${[['Format',report.format],['Architecture',report.architecture],['Sections',report.sections.length],['Win32 files',report.resources.length],['Overlay',report.overlaySize+' B']].map(([k,v])=>`<div><small>${esc(k)}</small><b>${esc(v)}</b></div>`).join('')}</div>`;
      body+=card('Evidence & export',`<div class="hash-line"><span>SHA-256</span><code>${esc(report.sha256)}</code></div><p class="note">Entry RVA ${hx(report.entryRva)} · ${esc(report.note)}</p>${(report.warnings||[]).map(w=>`<p class="lab-warning">${esc(w)}</p>`).join('')}${commands(btn('Export JSON report…','report')+(report.overlaySize?btn('Extract overlay…','overlay'):''))}`);
      body+=`<div class="lab-tabs" role="group" aria-label="Binary inspection views">${tabs.map(t=>btn(t==='resources'?'Win32 resources':t[0].toUpperCase()+t.slice(1),'tab',`data-tab="${t}" aria-pressed="${t===tab}"`)).join('')}</div>`;
      if(tab==='hex')return body+card('Hex viewport · 4096 bytes per request',`${field('offset','Raw file offset (decimal or 0x hex)',hexOffset)}${commands(btn('Read bytes','hex')+btn('Next 4096 bytes','hex-next'))}<pre class="lab-hex">${esc(hexText||'Enter an offset and read bytes explicitly.')}</pre>`);
      if(tab==='strings')body+=card('ASCII & UTF-16LE string leads',`${field('query','Filter text (case-insensitive)',query)}${commands(btn('Scan matching strings','strings'))}<p class="note">${esc(scanNote||'First 32 MiB; up to 2000 results; minimum 4 characters. UTF-16LE scan covers ASCII-range characters, not all Unicode. These are leads, not verified files.')}</p>`);
      const all=rows(), displayed=all.slice(offset,offset+100);
      const columns=tab==='sections'?['Section','Raw offset','Raw / virtual bytes','RVA / access','Actions']:tab==='exports'?['Export name','Ordinal','RVA / raw offset','Forwarder']:tab==='resources'?['Type / name / language','Raw offset','Bytes / codepage','Actions']:tab==='strings'?['Offset','Encoding','Text']:['Imported DLL'];
      const peek=n=>btn('Hex','peek',`data-offset="${n}"`);
      const line=(r,i)=>tab==='sections'?[esc(r.name),hx(r.offset),`${r.size} / ${r.virtualSize}`,`${hx(r.rva)} · ${esc(r.permissions)}`,r.valid?peek(r.offset)+btn('Extract…','section',`data-index="${offset+i}"`):'Invalid bounds']:tab==='exports'?[esc(r.name),r.ordinal,`${hx(r.rva)} / ${hx(r.offset)}`,esc(r.forwarder||'—')]:tab==='resources'?[esc(r.path),hx(r.offset),`${r.size} / ${r.codepage}`,peek(r.offset)+btn('Extract…','resource',`data-index="${offset+i}"`)]:tab==='strings'?[peek(r.offset)+' '+hx(r.offset),esc(r.encoding),esc(r.text)]:[esc(r)];
      return body+card(tab==='resources'?'Win32 resource tree leaves':'Inspection results',`<p class="note">${all.length?offset+1:0}–${Math.min(offset+100,all.length)} / ${all.length}${tab==='resources'?' · Numeric paths: type/name/language. Types: 3 icon, 6 string table, 10 raw data, 16 version, 24 manifest. Extracted bytes are raw resource payloads, not reconstructed ICO/BMP files.':''}${tab==='exports'?' · Named exports only; capped at 4096.':''}</p><div class="lab-table-wrap"><table class="lab-table"><thead><tr>${columns.map(c=>`<th>${c}</th>`).join('')}</tr></thead><tbody>${displayed.map((r,i)=>`<tr>${line(r,i).map(c=>`<td>${c}</td>`).join('')}</tr>`).join('')||`<tr><td colspan="5">No entries loaded for this view.</td></tr>`}</tbody></table></div>${commands((offset?btn('Previous','previous'):'')+(offset+100<all.length?btn('Next','next'):''))}`);
    }
    async function call(action,extra={}) {return request('binaryLab',{action,path:path.trim()||getState().exePath,sha256:report?.sha256,...extra});}
    async function action(name,el) {
      if(name==='browse'){const selected=await chooseFile('replace');if(selected){path=selected;report=null;}return;}
      if(name==='target'){path='';report=null;return;}
      if(name==='inspect'){report=null;stringRows=[];hexText='';scanNote='';offset=0;const result=await call('inspect');report=result;message='Inspection complete. Source is read-only.';return;}
      if(!report)throw Error('Inspect this binary first.');
      if(name==='tab'){tab=el.dataset.tab;offset=0;return;}
      if(name==='previous'){offset=Math.max(0,offset-100);return;}
      if(name==='next'){offset=Math.min(Math.max(0,Math.floor((rows().length-1)/100)*100),offset+100);return;}
      if(name==='strings'){const result=await call('strings',{query});stringRows=result.rows||[];scanNote=result.note+(result.limited?' Display/scan budget reached.':'');offset=0;return;}
      if(['hex','hex-next','peek'].includes(name)){
        if(name==='peek'){hexOffset=String(el.dataset.offset);tab='hex';}
        let at=Number(hexOffset);if(!Number.isSafeInteger(at)||at<0)throw Error('Enter a nonnegative decimal or 0x file offset.');
        if(name==='hex-next')at+=4096;
        const result=await call('hex',{offset:at});hexOffset=String(at);hexText=result.text;return;
      }
      const kind={section:'section',resource:'resource',overlay:'overlay',report:'report'}[name];
      if(kind){
        if(!await BsiEffects.confirm('Export '+kind+' to a NEW file? The original binary will not be modified. Existing destinations are refused.'))return;
        const destination=await chooseFile('extract');if(!destination)return;
        const result=await call('export',{kind,index:Number(el.dataset.index||0),destination});message=result.msg;return;
      }
      throw Error('Unknown Binary Lab action.');
    }
    return {render,action,input(el){const key=el.dataset.editorField;if(key==='lab:path'){path=el.value;report=null;stringRows=[];hexText='';}if(key==='lab:query')query=el.value;if(key==='lab:offset')hexOffset=el.value;},reset(){report=null;path='';tab='sections';offset=0;query='';hexOffset='0';hexText='';stringRows=[];scanNote='';message='';}};
  }};
})();
