const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const context={window:{}};vm.runInNewContext(fs.readFileSync('src/web/emulator-icons.js','utf8'),context);
const pack=context.window.BsiEmulatorIcons;
assert.equal(pack.icons.length,22);assert.equal(new Set(pack.icons.map(i=>i.id)).size,22);
for(const icon of pack.icons){
 const svg=pack.svg(icon.id);
 assert.equal(fs.readFileSync(`assets/emulator-icons/outline/${icon.id}.svg`,'utf8').trim(),svg,'Export drift: '+icon.id);
 assert.match(svg,/viewBox="0 0 24 24"/);
 assert.ok(!/<(?:script|foreignObject|image|style|text)\b|currentColor|(?:href|onload)=/i.test(svg));
 assert.ok(Buffer.byteLength(svg)<1024);
 const custom=pack.svg(icon.id,{color:'#AABBCC',stroke:'1.4',width:18,height:20});
 assert.ok(custom.includes('stroke="#AABBCC"')&&custom.includes('width="18" height="20"'));
 assert.ok(!custom.includes('{color}'));
}
assert.throws(()=>pack.svg('not-an-icon'));assert.throws(()=>pack.svg('home',{color:'url(https://bad)'}));assert.throws(()=>pack.svg('home',{stroke:'999'}));
assert.ok(pack.svg('home',{width:Infinity,height:99999}).includes('width="24" height="24"'));
assert.ok(pack.eligible({path:'images/Home.SVG',text:true}));assert.ok(!pack.eligible({path:'home.png',text:true}));assert.ok(!pack.eligible({path:'home.svg',text:false}));
const html=fs.readFileSync('src/web/index.html','utf8'),cmake=fs.readFileSync('CMakeLists.txt','utf8');
assert.ok(html.includes('emulator-icons.js')&&cmake.includes('src/web/emulator-icons.js'));
const app=fs.readFileSync('src/web/app.js','utf8');
const stage=app.slice(app.indexOf("if (name === 'stageEmulatorIcon')"),app.indexOf("if (name === 'native')"));
assert.ok(stage.includes('BsiEffects.confirm')&&stage.includes('dirty=true'));
assert.ok(!stage.includes('request('),'Staging must not write through any native RPC');
console.log('PASS: 22 original SVGs, deterministic exports, safe style inputs, SVG-only eligibility, embedded asset and draft-only staging.');
