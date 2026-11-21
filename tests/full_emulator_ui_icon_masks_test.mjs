// Synchronous icon bytes must be valid PNGs, not UTF-8-converted binary strings.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import zlib from 'node:zlib';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const assets=path.join(root,'assets/full-emulator-ui');
const context=vm.createContext({decodeURIComponent});
vm.runInContext(fs.readFileSync(path.join(assets,'IconMasks.js'),'utf8').replace(/^\.pragma library\s*/,''),context);
function crc(bytes){let c=0xffffffff;for(const byte of bytes)c=context.crcTable[(c^byte)&255]^(c>>>8);return (c^0xffffffff)>>>0;}
function chunks(data){
 assert.deepEqual(data.subarray(0,8),Buffer.from('89504e470d0a1a0a','hex'));
 let out={},at=8;
 while(at<data.length){const n=data.readUInt32BE(at),type=data.toString('ascii',at+4,at+8),body=data.subarray(at+8,at+8+n);
  assert.equal(crc(data.subarray(at+4,at+8+n)),data.readUInt32BE(at+8+n),type+' CRC');
  out[type]=out[type]?Buffer.concat([out[type],body]):body;at+=n+12;
 }
 assert.equal(at,data.length);return out;
}
function originalAlpha(data){
 const c=chunks(data),width=c.IHDR.readUInt32BE(0),height=c.IHDR.readUInt32BE(4),stride=width*4;
 assert.equal(c.IHDR[8],8);assert.equal(c.IHDR[9],6);
 const raw=zlib.inflateSync(c.IDAT);let before=Buffer.alloc(stride),alpha=Buffer.alloc((width+1)*height),at=0;
 for(let y=0;y<height;y++){
  const mode=raw[at++],row=Buffer.from(raw.subarray(at,at+stride));at+=stride;
  for(let x=0;x<stride;x++){
   const a=x>=4?row[x-4]:0,b=before[x],d=x>=4?before[x-4]:0;
   let predict=0;
   if(mode===1)predict=a;
   else if(mode===2)predict=b;
   else if(mode===3)predict=(a+b)>>1;
   else if(mode===4){const p=a+b-d,pa=Math.abs(p-a),pb=Math.abs(p-b),pd=Math.abs(p-d);predict=pa<=pb&&pa<=pd?a:pb<=pd?b:d;}
   else assert.equal(mode,0);
   row[x]=(row[x]+predict)&255;
  }
  for(let x=0;x<width;x++)alpha[y*(width+1)+1+x]=row[x*4+3];before=row;
 }
 return alpha;
}
for(let n=0;n<10;n++){const bytes=Buffer.from(Array.from({length:n*64},(_,i)=>i%256));assert.equal(context.base64(bytes.toString('binary')),bytes.toString('base64'));}
for(const key of Object.keys(context.masks)){
 const source='file:///Test%20Player/'+key,uri=context.colored(source,{r:130/255,g:223/255,b:177/255});
 const data=Buffer.from(uri.split(',')[1],'base64'),c=chunks(data);
 assert.equal(c.IHDR[9],3);assert.equal(c.IHDR[8],8);
 assert.deepEqual(c.PLTE,Buffer.from(Array(256).fill([130,223,177]).flat()),key+' exact custom tint');
 assert.deepEqual(c.tRNS,Buffer.from(Array.from({length:256},(_,i)=>i)));
 assert.deepEqual(zlib.inflateSync(c.IDAT),originalAlpha(fs.readFileSync(path.join(assets,key))),key+' alpha');
}
assert.equal(context.colored('file:///Unknown.png',{r:1,g:1,b:1}),'file:///Unknown.png');
for(let i=0;i<150;i++)context.colored('file:///Test/SharedIcons/advanced.png',{r:i/255,g:1,b:1});
assert(context.order.length<=128);assert(Object.keys(context.cache).length<=128);
console.log('PASS: 94 indexed PNGs, CRCs, exact tint/alpha, binary Base64, nested Settings paths and bounded tint cache.');
