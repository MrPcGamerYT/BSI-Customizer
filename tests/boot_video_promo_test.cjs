// JS control-flow and generated-source checks; not a QML/media runtime test.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const Bsi=require('../tools/player-chrome/core.js');
const source=fs.readFileSync('downloads/BootVideo-Promo/BootVideo.qml','utf8');
const info=Bsi.scan(source);
function body(name){const start=source.indexOf('function '+name+'('),open=source.indexOf('{',start);return source.slice(open+1,info.pairs.get(open));}
assert.ok(source.startsWith('import QtQuick 2.15'));
assert.ok(!/import\s+QtWebEngine/.test(source));
assert.ok(source.includes('property bool withVideo: true'));
assert.ok(source.includes('Qt.resolvedUrl("Promo.mp4")'));
for(const mode of ['qt6','qt5','missing','disabled']){
 const calls=[],errors=[];
 const context={withVideo:mode!=='disabled',videoPlaying:false,diagnostic:'',playerItem:null,videoSource:{toString:()=>"file:///C:/Program%20Files/MSI/Assets/Promo.mp4"},console:{warn:e=>errors.push(e)},Qt:{createQmlObject:(text,parent,label)=>{
  calls.push({text,label});Bsi.scan(text);
  if(mode==='missing'||mode==='qt5'&&label==='bsiPromoQt6')throw Error('fixture missing module');
  return {destroy(){this.destroyed=true;}};
 }}};
 context.root=context;vm.createContext(context);
 context.reportError=vm.runInContext('(function(message){'+body('reportError')+'})',context);
 const start=vm.runInContext('(function(){'+body('startVideo')+'})',context);
 start();
 if(mode==='disabled'){assert.equal(calls.length,0);assert.equal(context.diagnostic,'Boot video disabled.');continue;}
 assert.equal(calls.length,mode==='qt6'?1:2);
 for(const {text}of calls){assert.ok(text.includes('z: 10'));assert.ok(text.includes('visible: root.videoPlaying'));assert.ok(text.includes('source: "file:///C:/Program%20Files/MSI/Assets/Promo.mp4"'));assert.ok(text.includes('Component.onCompleted: play()'));}
 if(mode==='missing'){assert.ok(context.diagnostic.includes('No compatible QtMultimedia'));assert.equal(context.playerItem,null);}
 else {const old=context.playerItem;context.withVideo=false;start();assert.ok(old.destroyed);assert.equal(context.playerItem,null);assert.equal(context.diagnostic,'Boot video disabled.');}
 context.videoPlaying=true;context.reportError('decoder failure');assert.equal(context.videoPlaying,false);assert.ok(context.diagnostic.includes('decoder failure'));
}
console.log('PASS: video-first JS flow, Qt6/Qt5 creation paths, disabled mode, error reporting, cleanup, quoted file URL and generated-source balance. NOT actual QML/MP4 playback.');
