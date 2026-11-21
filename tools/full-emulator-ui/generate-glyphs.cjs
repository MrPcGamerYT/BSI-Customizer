// npm install --prefix .cache/glyphgen --no-audit --no-fund @resvg/resvg-js@2.6.2
// node tools/full-emulator-ui/generate-glyphs.cjs
// Offline at install time: generated, transparent PNGs are shipped with the pack.
const fs = require('fs'), path = require('path'), vm = require('vm');
const { Resvg } = require('../../.cache/glyphgen/node_modules/@resvg/resvg-js');
const root = path.resolve(__dirname,'../../assets/full-emulator-ui');
const context = vm.createContext({encodeURIComponent});
vm.runInContext(fs.readFileSync(path.join(root,'Icons.js'),'utf8').replace(/^\.pragma library\s*/,''),context);
function png(folder,name,svg) {
  const dir=path.join(root,folder);fs.mkdirSync(dir,{recursive:true});
  fs.writeFileSync(path.join(dir,name+'.png'),new Resvg(svg,{fitTo:{mode:'width',value:64}}).render().asPng());
}
for(const name of Object.keys(context.shapes)) png('SharedIcons',name,context.svg(name,'#eeeef4'));
const paths = {
 Tap:'<circle cx="12" cy="12" r="8"/><circle cx="12" cy="12" r="2"/>',
 RepeatedTap:'<circle cx="12" cy="12" r="8"/><path d="M5 4 3 5M19 4l2 1M4 19l1 2M19 20l2-1"/><circle cx="12" cy="12" r="4"/>',
 Dpad:'<path d="M9 3h6v6h6v6h-6v6H9v-6H3V9h6Z"/>',
 Aim:'<circle cx="12" cy="12" r="7"/><circle cx="12" cy="12" r="2"/><path d="M12 1v5M12 18v5M1 12h5M18 12h5"/>',
 FreeLook:'<path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12Z"/><circle cx="12" cy="12" r="3"/>',
 Script:'<path d="m8 6-6 6 6 6M16 6l6 6-6 6M14 3l-4 18"/>',
 Swipe:'<path d="M7 16V9a2 2 0 0 1 4 0v4-9a2 2 0 0 1 4 0v9-6a2 2 0 0 1 4 0v9c0 4-4 6-7 6-4 0-7-3-9-6a2 2 0 0 1 3-2l3 3M2 4h5M5 2l2 2-2 2"/>',
 Zoom:'<circle cx="10" cy="10" r="7"/><path d="m15 15 6 6M6 10h8M10 6v8"/>',
 Tilt:'<rect x="6" y="3" width="12" height="18" rx="3" transform="rotate(-24 12 12)"/><path d="m2 6 2-4 4 1M22 18l-2 4-4-1"/>',
 'MOBA-PAD':'<circle cx="12" cy="12" r="9"/><path d="m12 5-2 3h4ZM12 19l-2-3h4ZM5 12l3-2v4ZM19 12l-3-2v4Z"/>',
 SkillPad:'<circle cx="12" cy="12" r="8"/><path d="m12 4 7 8-7 8-7-8Z"/><circle cx="12" cy="12" r="2"/>',
 Rotate:'<path d="M20 8a8 8 0 1 0 0 8M20 3v5h-5"/>',
 EdgeScroll:'<rect x="3" y="3" width="18" height="18" rx="3"/><path d="M6 6h3M6 6v3M18 6h-3M18 6v3M6 18h3M6 18v-3M18 18h-3M18 18v-3"/>',
 Scroll:'<rect x="4" y="3" width="16" height="18" rx="2"/><path d="M16 3v18M18 7v5"/>',
 MouseZoom:'<rect x="6" y="2" width="12" height="20" rx="6"/><path d="M6 10h12M12 2v8M12 5v2"/>',
 State:'<circle cx="12" cy="12" r="8"/><circle cx="12" cy="12" r="3"/>',
 DontForwardKeys:'<rect x="2" y="5" width="20" height="14" rx="3"/><path d="m3 3 18 18M6 10h2M11 10h2M16 10h2"/>',
 SendOriginalKeys:'<rect x="2" y="5" width="20" height="14" rx="3"/><path d="M6 10h2M11 10h2M16 10h2M8 15h8"/>',
 Callback:'<path d="M6 8h10a5 5 0 0 1 0 10h-4M6 3 1 8l5 5"/>',
 MouseLock:'<rect x="5" y="9" width="14" height="12" rx="3"/><path d="M8 9V6a4 4 0 0 1 8 0v3M12 14v3"/>',
 SmartScroll:'<circle cx="12" cy="12" r="8"/><path d="m9 7 3-3 3 3M9 17l3 3 3-3M7 9l-3 3 3 3M17 9l3 3-3 3"/>',
 NativeCursor:'<path d="M5 2v18l5-5 4 7 3-2-4-7h7Z"/>'
};
for(const [name,shape] of Object.entries(paths)) png('ControlIcons',name,`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#eeeef4" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">${shape}</svg>`);
console.log('Generated',Object.keys(context.shapes).length,'complete shared glyphs and',Object.keys(paths).length,'controls-editor glyphs.');
