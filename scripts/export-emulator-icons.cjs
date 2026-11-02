// Generate the distributable SVGs from the same trusted library the editor uses.
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const root=path.resolve(__dirname,'..'), context={window:{}};
vm.runInNewContext(fs.readFileSync(path.join(root,'src/web/emulator-icons.js'),'utf8'),context);
const pack=context.window.BsiEmulatorIcons, dir=path.join(root,'assets/emulator-icons/outline');
fs.mkdirSync(dir,{recursive:true});
for(const icon of pack.icons)fs.writeFileSync(path.join(dir,icon.id+'.svg'),pack.svg(icon.id)+'\n');
fs.writeFileSync(path.join(root,'assets/emulator-icons/manifest.json'),JSON.stringify({name:'BSI Outline',version:1,viewBox:'0 0 24 24',icons:pack.icons.map(({id,label,area,term})=>({id,label,area,searchHint:term,file:'outline/'+id+'.svg'}))},null,2)+'\n');
console.log('Exported '+pack.icons.length+' original SVGs.');
