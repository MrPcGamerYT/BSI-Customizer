/* Metadata-only explorer model. No bridge, decoder, file writes or SVG DOM. */
(() => {
  'use strict';
  const normalized = path => String(path || '').replace(/\\/g, '/').replace(/^\/+|\/+$/g, '');
  const collator = new Intl.Collator(undefined, {numeric:true, sensitivity:'base'});
  function create() {
    let rows=[], index=[], folders=new Map(), ordered=[], cacheKey='', cache=[];
    const bookmarks=new Set(), checked=new Set();
    function setRows(next) {
      rows=next; index=BsiResourceSearch.prepare(rows); folders=new Map([['',{path:'',name:'All resources',count:rows.length,children:new Set()}]]);
      for(const row of rows) {
        const parts=normalized(row.path).split('/').slice(0,64); parts.pop();
        let parent='';
        for(const part of parts) {
          const path=parent?parent+'/'+part:part;
          if(!folders.has(path))folders.set(path,{path,name:part,count:0,children:new Set()});
          folders.get(parent).children.add(path); ++folders.get(path).count; parent=path;
        }
      }
      ordered=rows.slice().sort((a,b)=>collator.compare(a.path,b.path)||a.index-b.index);
      cacheKey=''; checked.clear();
      const paths=new Set(rows.map(row=>row.path));
      for(const path of bookmarks)if(!paths.has(path))bookmarks.delete(path);
    }
    function query({text='',kind=0,folder='',sort='path',favorites=false}={}) {
      const key=JSON.stringify([text,kind,folder,sort,favorites,[...bookmarks]]);
      if(key===cacheKey)return cache;
      const allowed=new Set(BsiResourceSearch.filter(index,text,kind).map(row=>row.index));
      const prefix=normalized(folder);
      const filtered=ordered.filter(row=>allowed.has(row.index)&&(!prefix||normalized(row.path).startsWith(prefix+'/'))&&(!favorites||bookmarks.has(row.path)));
      if(sort==='size')filtered.sort((a,b)=>Number(b.size)-Number(a.size)||collator.compare(a.path,b.path));
      if(sort==='type')filtered.sort((a,b)=>collator.compare(a.format,b.format)||collator.compare(a.path,b.path));
      cacheKey=key;cache=filtered;return filtered;
    }
    function children(folder='') {return [...(folders.get(folder)?.children||[])].map(path=>folders.get(path)).sort((a,b)=>collator.compare(a.name,b.name));}
    const toggle=(set,path)=>{if(set.has(path))set.delete(path);else set.add(path);cacheKey='';};
    return {setRows,query,children,bookmarks,checked,normalize:normalized,
      bookmark:path=>toggle(bookmarks,path),check:path=>toggle(checked,path),
      reset(){rows=[];index=[];folders.clear();ordered=[];cache=[];cacheKey='';bookmarks.clear();checked.clear();}};
  }
  window.BsiResourceWorkbench={create,normalized};
})();
