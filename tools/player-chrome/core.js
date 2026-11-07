/* BSI player chrome source transformer. No EXE writes, network or backend replacement. */
(function (global) {
'use strict';
function scan(text) {
    const out=text.split(''), spans=[];
    let i=0;
    while(i<text.length) {
        const start=i; let special=true;
        if(text.startsWith('//',i)) { const end=text.indexOf('\n',i); i=end<0?text.length:end; }
        else if(text.startsWith('/*',i)) {const end=text.indexOf('*/',i+2); if(end<0)throw Error('Unclosed QML comment');i=end+2;}
        else if('"\'`'.includes(text[i])) {
            const quote=text[i++]; let closed=false;
            while(i<text.length) {if(text[i]==='\\')i+=2;else if(text[i++]===quote){closed=true;break;}}
            if(!closed)throw Error('Unclosed quoted value');
        } else if(text[i]==='/') {
            const prev=out.slice(0,i).join('').trimEnd();
            if(!prev || '=(:,![&|?;{'.includes(prev.slice(-1)) || /\b(return|case)\s*$/.test(prev)) {
                i++;let bracket=false,closed=false;
                while(i<text.length) {
                    const c=text[i]; if(c==='\n'||c==='\r')throw Error('Unclosed JS regex');
                    if(c==='\\'){i+=2;continue;}
                    if(c==='[')bracket=true;else if(c===']')bracket=false;
                    else if(c==='/'&&!bracket){i++;while(/[a-z]/i.test(text[i]||' ')&&i<text.length)i++;closed=true;break;}
                    i++;
                }
                if(!closed)throw Error('Unclosed JS regex');
            } else {i++;special=false;}
        } else {i++;special=false;}
        if(special) {spans.push([start,Math.min(i,text.length)]);for(let j=start;j<Math.min(i,text.length);j++)if(!'\r\n'.includes(text[j]))out[j]=' ';}
    }
    const code=out.join(''),depth=new Int32Array(code.length),pairs=new Map(),stack=[];
    for(let p=0;p<code.length;p++) {depth[p]=stack.length;if(code[p]==='{')stack.push(p);else if(code[p]==='}') {if(!stack.length)throw Error('Unbalanced QML braces');pairs.set(stack.pop(),p);}}
    if(stack.length)throw Error('Unbalanced QML braces');
    return {code,depth,pairs,spans};
}
function binding(text, info, node, name, optional=false) {
    const pattern=new RegExp('(?:^|\\n)[ \\t]*'+name+'[ \\t]*:', 'g');
    const region=info.code.slice(node.open+1,node.end),hits=[];
    for(const m of region.matchAll(pattern)) {
        const p=node.open+1+m.index;
        if(info.depth[p]!==info.depth[node.open]+1)continue;
        const colon=info.code.indexOf(':',p);
        let end=text.indexOf('\n',colon);if(end<0||end>node.end)end=node.end;
        // Only an entire single-line property expression is supported; never guess.
        const raw=text.slice(colon+1,end),masked=info.code.slice(colon+1,end);
        let cutoff=masked.indexOf(';');if(cutoff>=0)end=colon+1+cutoff;
        const expression=text.slice(colon+1,end).trim();
        if(!expression)throw Error('Empty '+name+' binding');
        hits.push({start:colon+1,end,expression});
    }
    if(!hits.length&&optional)return null;
    if(hits.length!==1)throw Error('Expected one direct '+name+' binding');
    return hits[0];
}
function objects(info,type,range=null) {
    const pattern=new RegExp('\\b'+type+'\\s*\\{','g'), found=[];
    for(const m of info.code.matchAll(pattern)) {
        const open=info.code.indexOf('{',m.index),end=info.pairs.get(open);
        if(range&&!(range.open<open&&end<range.end))continue;
        found.push({start:m.index,open,end});
    }
    return found;
}
function named(text,info,type,id,range=null) {
    const matches=objects(info,type,range).filter(n=>{const b=binding(text,info,n,'id',true);return b&&b.expression===id;});
    if(matches.length!==1)throw Error('Expected one '+type+' with id '+id+'; refusing an unknown layout');
    return matches[0];
}
function rootObject(text,info,type,id) {
    const roots=objects(info,type).filter(n=>info.depth[n.open]===0);
    if(roots.length!==1)throw Error('Expected exactly one '+type+' root');
    if(binding(text,info,roots[0],'id').expression!==id)throw Error('Wrong QML resource root');
    return roots[0];
}
function replace(edits,b,value){edits.push({start:b.start,end:b.end,text:' '+value});}
function setColor(text,info,node,value,edits){const b=binding(text,info,node,'color',true);if(b)replace(edits,b,JSON.stringify(value));else edits.push({start:node.open+1,end:node.open+1,text:'\ncolor:'+JSON.stringify(value)+'\n'});}
function enable(text,info,node,name,edits) {
    const b=binding(text,info,node,name), m=/^\s*(true|false)\b/.exec(info.code.slice(b.start,b.end));
    if(!m||info.code.slice(b.start+m[0].length,b.end).trim())throw Error('Unsupported visibility expression for '+name);
    const first=b.start+m[0].lastIndexOf(m[1]);
    edits.push({start:first,end:first+m[1].length,text:'true'}); // Keep original clean markers/comments.
}
function apply(text,edits) {
    const sorted=edits.slice().sort((a,b)=>b.start-a.start||b.end-a.end);
    for(let i=1;i<sorted.length;i++)if(sorted[i].end>sorted[i-1].start)throw Error('Overlapping source edits refused');
    for(const e of sorted)text=text.slice(0,e.start)+e.text+text.slice(e.end);
    return text;
}
function compact(text) {
    const info=scan(text);let offset=0;
    return text.split(/(?<=\n)/).map(row=>{
        const indent=/^[ \t]*/.exec(row)[0].length, p=offset+indent;
        offset+=row.length;
        return p<info.code.length&&!/\s/.test(info.code[p])?row.slice(indent):row;
    }).join('');
}
function guardedDown(id){return '("down" in '+id+' && typeof '+id+'.down === "boolean") ? '+id+'.down : (("pressed" in '+id+' && typeof '+id+'.pressed === "boolean") ? '+id+'.pressed : false)';}
function patchTopbar(source,face,options={}) {
    if(/\bBsiChromeFace\b|BSI MONO CHROME/.test(source))throw Error('Already patched. Use your original resource backup.');
    const text=source.replace(/^\uFEFF/,''),info=scan(text),root=rootObject(text,info,'Rectangle','iRoot');
    const row=named(text,info,'UiRowLayout','iTopbarRow',root),edits=[];
    const size=binding(text,info,root,'property int pIconSize',true);
    if(!size||size.expression!=='32')throw Error('Expected the native 32px pIconSize; no geometry guessing.');
    setColor(text,info,root,'#080809',edits);setColor(text,info,row,'#080809',edits);
    const caption=named(text,info,'Text','iBstTxt',row);setColor(text,info,caption,'#f1f1f5',edits);
    const specs=[['iBackBtn','back'],['iHomeBtn','home'],['iRecentBtn','recent'],['iMinimizeBtn','minimize'],['iFixedResizeBtn','resize'],['iCloseBtn','close'],['iSidebarShowBtn','sidebar']];
    const cfg=named(text,info,'Item','iCfgBtn',row);
    const cfgButtons=objects(info,'UiImageButton',cfg).filter(n=>info.depth[n.open]===info.depth[cfg.open]+1);
    if(cfgButtons.length!==1)throw Error('Ambiguous native menu button');
    const menu=cfgButtons[0],menuId=binding(text,info,menu,'id',true);
    specs.push([menuId?menuId.expression:'bsiChromeMenu','menu',menu,!menuId]);
    let sequence=0;
    for(const [id,kind,provided,newId] of specs) {
        const node=provided||named(text,info,'UiImageButton',id,row),asset=binding(text,info,node,'asset');
        const expression=asset.expression;
        const masked=scan('Item { property var v: '+expression+'\n}').code;
        if(/[?:+*/|&.,]\s*$/.test(masked)&&!/["']\s*$/.test(expression))throw Error('Unsupported multiline native asset binding');
        const skin='bsiChromeGlyph'+sequence++;
        replace(edits,asset,skin+'.ready ? "" : (\n'+expression+'\n)');
        let extra=newId?'\nid:'+id+'\n':'';
        for(const dimension of ['width','height'])if(!binding(text,info,node,dimension,true))extra+='\n'+dimension+':pIconSize\n';
        extra+='\nBsiChromeFace {\nid:'+skin+';anchors.fill:parent\nkind:'+JSON.stringify(kind)+'\nstock:('+expression+'\n)\nhot:'+id+'.hovered\ndownState:'+guardedDown(id)+'\nusable:'+id+'.enabled\n}\n';
        edits.push({start:node.open+1,end:node.open+1,text:extra});
    }
    if(options.restoreControls!==false) {
        enable(text,info,cfg,'visible',edits);
        const collapse=named(text,info,'UiImageButton','iSidebarShowBtn',row);enable(text,info,collapse,'visible',edits);
        const models=objects(info,'PlrTopbarElement',root).filter(n=>/\biCfgBtn\.visible\s*=\s*visibility\b/.test(info.code.slice(n.open,n.end)));
        if(models.length!==1)throw Error('Expected exactly one native menu visibility model');
        enable(text,info,models[0],'pVisible',edits);
    }
    edits.push({start:root.open+1,end:root.open+1,text:'\n// BSI MONO CHROME V1: native handlers and 32px geometry retained.\n'+face+'\n'});
    let result=apply(text,edits);if(options.compact!==false)result=compact(result);
    scan(result);return {text:result,sites:sequence,restored:options.restoreControls!==false};
}
function patchWindow(source,options={}) {
    if(source.includes('BSI MONO FRAME V1'))throw Error('Window already patched. Use the original backup.');
    const text=source.replace(/^\uFEFF/,''),info=scan(text),root=rootObject(text,info,'ApplicationWindow','iAppwin');
    const edits=[];
    const mainCandidates=objects(info,'Rectangle',root).filter(n=>info.depth[n.open]===info.depth[root.open]+1).filter(n=>{const id=binding(text,info,n,'id',true);return id&&id.expression==='iMainContentLayout';});
    if(mainCandidates.length!==1)throw Error('Expected the native main content rectangle');
    const main=mainCandidates[0],top=named(text,info,'PlrTopbar','iTopbar',main),sidebar=named(text,info,'PlrSidebar','iSidebar',main);
    for(const [node,color] of [[root,'#34343b'],[main,'#050506'],[top,'#080809'],[sidebar,'#080809']])setColor(text,info,node,color,edits);
    edits.push({start:root.open+1,end:root.open+1,text:'\n// BSI MONO FRAME V1: colors only; native viewport, flags and offsets retained.\n'});
    let result=apply(text,edits);if(options.compact!==false)result=compact(result);scan(result);
    return {text:result,sites:4};
}
function fingerprint(text) {
    const info=scan(text),literal=info.spans.map(([a,b])=>text.slice(a,b));
    return JSON.stringify([info.code.replace(/\s/g,''),literal]);
}
function nativeBodies(text) {
    const info=scan(text), result=[];
    const face=/component\s+BsiChromeFace\s*:\s*Item\s*\{/.exec(info.code);
    const limits=face?[face.index,info.pairs.get(info.code.indexOf('{',face.index))]:[-1,-1];
    const pattern=/\bfunction\s+\w+\s*\([^)]*\)\s*\{|\bon\w+\s*:\s*(?:function\s*\([^)]*\))?\s*\{/g;
    for(const m of info.code.matchAll(pattern)){
        if(limits[0]<m.index&&m.index<limits[1])continue;
        const open=info.code.indexOf('{',m.index);result.push(fingerprint(text.slice(m.index,info.pairs.get(open)+1)));
    }
    return result;
}
const api={scan,binding,objects,named,compact,patchTopbar,patchWindow,nativeBodies,fingerprint};
if(typeof module!=='undefined'&&module.exports)module.exports=api;else global.BsiChrome=api;
})(typeof globalThis!=='undefined'?globalThis:this);
