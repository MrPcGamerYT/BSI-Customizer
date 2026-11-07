/* Run with the repository's cached playwright-core and a real Chromium executable. */
const assert=require('node:assert/strict');
const fs=require('node:fs');const path=require('node:path');const {pathToFileURL}=require('node:url');
const root=path.resolve(__dirname,'..');
const {chromium}=require(path.join(root,'.cache/player-chrome-deps/node_modules/playwright-core'));
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/tmp/chromium',headless:true,args:['--no-sandbox','--disable-dev-shm-usage','--disable-gpu']});
 try{
 const page=await browser.newPage({viewport:{width:1200,height:1000},acceptDownloads:true});
 const errors=[],external=[];page.on('pageerror',e=>errors.push(String(e)));page.on('request',r=>{if(/^https?:/.test(r.url()))external.push(r.url());});
 await page.goto(pathToFileURL(path.join(root,'downloads/Player-Chrome-Monochrome/PlayerChrome.html')).href);
 await page.click('#build');await page.waitForFunction(()=>document.querySelector('#status').textContent.includes('BOTH'));
 await page.setInputFiles('#topFile',path.join(__dirname,'fixtures/player-chrome/PlrTopbar.qml'));
 await page.setInputFiles('#winFile',path.join(__dirname,'fixtures/player-chrome/plr.qml'));
 await page.waitForFunction(()=>document.querySelector('#winSource').value.includes('ApplicationWindow'));
 await page.click('#build');await page.waitForSelector('#result:not([hidden])');console.log('Built:',await page.textContent('#status'));
 assert.ok((await page.textContent('#capacityReport')).includes('SLOT UNKNOWN — FIT UNVERIFIED'));
 assert.ok(await page.isDisabled('#saveTop'));
 await page.check('#ack');assert.ok(await page.isEnabled('#saveTop'));
 const downloadEvent=page.waitForEvent('download');await page.click('#saveTop');const download=await downloadEvent;
 assert.equal(download.suggestedFilename(),'PlrTopbar.qml');
 const saved=await download.path(),actual=fs.readFileSync(saved,'utf8');
 assert.equal(actual,fs.readFileSync(path.join(root,'.cache/player-chrome/PlrTopbar.qml'),'utf8'));
 const sizes=await page.evaluate(()=>({top:estimated(document.querySelector('#topOutput').value),win:estimated(document.querySelector('#winOutput').value)}));
 fs.writeFileSync(path.join(root,'.cache/player-chrome/browser-sizes.json'),JSON.stringify(sizes));
 await page.fill('#topCap','1');assert.ok(await page.isHidden('#result'));await page.click('#build');
 console.log('Budget guard:',await page.textContent('#status'));await page.waitForFunction(()=>document.querySelector('#status').textContent.includes('exceeds 1'));
 assert.ok(await page.isHidden('#result'));assert.ok(await page.isDisabled('#saveTop'));
 console.log('Estimates',sizes);await page.fill('#topCap',String(sizes.top));await page.fill('#winCap',String(sizes.win));await page.click('#build');
 await page.waitForSelector('#result:not([hidden])');assert.ok((await page.textContent('#capacityReport')).includes('supplied slot'));
 await page.check('#ack');await page.click('#copyTop');await page.waitForFunction(()=>/copied|selected/.test(document.querySelector('#status').textContent));
 await page.locator('details summary').first().click();const topOutput=await page.inputValue('#topOutput');await page.fill('#topSource',topOutput);await page.click('#build');
 console.log('Repeat guard:',await page.textContent('#status'));await page.waitForFunction(()=>document.querySelector('#status').textContent.includes('Already patched'));
 assert.ok(await page.isHidden('#result'));
 assert.deepEqual(errors,[]);assert.deepEqual(external,[]);
 console.log('PASS: real Chromium file:// UI, inputs, preservation, full QML download bytes, unknown capacity warnings, over-budget refusal, exact estimated budget, stale-output invalidation, no network requests');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exit(1);});
