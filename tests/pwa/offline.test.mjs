import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';
import {webcrypto} from 'node:crypto';
import {dirname,join} from 'node:path';
import {fileURLToPath} from 'node:url';

// Exercise the generated source itself in a deterministic CacheStorage harness.
const sourcePath = process.env.NETTOOLS_SW_FILE || fileURLToPath(new URL('../../website-preview/dist/app/sw.js', import.meta.url));
const source = readFileSync(sourcePath, 'utf8');
function harness({failInstall=false,votes=[],corrupt=false}={}) {
  const handlers = {}, stores = new Map();
  let skipped=0, claimed=0, fetched=0;
  const cacheStorage = {
    async open(key) {
      if (!stores.has(key)) stores.set(key,new Map());
      const map=stores.get(key);
      return {async match(url) {return map.get(url)?.clone()}, async put(url,response) {map.set(url,response)}, async addAll(requests) {
        for (const request of requests) {
          map.set(request.url, new Response('cached ' + request.url));
          if (failInstall) throw new Error('network failed');
        }
      }};
    },
    async keys(){return [...stores.keys()]},
    async delete(key){return stores.delete(key)},
  };
  const notices=[];
  const windows=votes.map((ready,index)=>({id:String(index),url:'https://example.test/NetTools/app/',
    postMessage(message){notices.push(message); if(message.type==='UPDATE_QUERY' && ready!==null)
      queueMicrotask(()=>handlers.message({data:{type:'UPDATE_VOTE',nonce:message.nonce,ready},source:{id:String(index)},waitUntil(){}}));}}));
  const self={registration:{scope:'https://example.test/NetTools/app/'},
    addEventListener(type,fn){handlers[type]=fn},
    async skipWaiting(){skipped++},clients:{async claim(){claimed++},async matchAll(){return windows}}};
  vm.runInNewContext(source,{self,caches:cacheStorage,URL,Request,Response,AbortController,crypto:webcrypto,setTimeout:(fn)=>setTimeout(fn,20),clearTimeout,
    fetch:async(request)=>{fetched++; if(failInstall) throw new Error('network failed'); return new Response(corrupt ? 'wrong version' : readFileSync(join(dirname(sourcePath),new URL(request.url || request).pathname.split('/app/')[1])))}});
  async function event(type,extra={}) {
    const work=[];
    handlers[type]({waitUntil(p){work.push(p)},...extra});
    await Promise.all(work);
  }
  async function status() {
    let result;
    await event('message',{data:{type:'STATUS'},ports:[{postMessage(value){result=value}}]});
    return result;
  }
  return {stores,event,status,self,notices,windows,setCorrupt(value){corrupt=value},async fetch(url,mode='cors',method='GET') {
    let response;
    handlers.fetch({request:{url,mode,method},respondWith(p){response=p}});
    return response;
  },get skipped(){return skipped},get claimed(){return claimed},get fetched(){return fetched}};
}

test('offline readiness requires the complete shell and installation never forces an update',async()=>{
  const h=harness();
  assert.equal((await h.status()).ready,false);
  await h.event('install');
  assert.equal((await h.status()).ready,true);
  assert.equal(h.skipped,0);
  const cache=[...h.stores.values()][0];
  cache.delete('https://example.test/NetTools/app/engine.wasm');
  assert.equal((await h.status()).ready,false);
  await h.event('message',{data:{type:'ACTIVATE'}});
  assert.equal(h.skipped,0);
});

test('failed shell installation removes the candidate cache',async()=>{
  const h=harness({failInstall:true});
  await assert.rejects(h.event('install'),/network failed/);
  assert.equal(h.stores.size,0);
  assert.equal(h.skipped,0);
});

test('app navigation is offline, learning centre and public IP bypass the worker',async()=>{
  const h=harness();
  await h.event('install');
  const response=await h.fetch('https://example.test/NetTools/app/','navigate');
  assert.equal(await response.text(),readFileSync(join(dirname(sourcePath),'index.html'),'utf8'));
  assert.ok(h.fetched>0);
  for (const url of ['https://api.ipify.org/?format=json','https://api6.ipify.org/?format=json',
    'https://example.test/NetTools/','https://example.test/NetTools/lessons/example.html',
    'https://example.test/NetTools/app/unknown.json']) {
    assert.equal(await h.fetch(url),undefined);
  }
  assert.equal(await h.fetch('https://example.test/NetTools/app/index.html','cors','POST'),undefined);
});

test('activation retains the prior app cache without claiming other registrations',async()=>{
  const h=harness();
  h.stores.set('learning-centre',new Map());
  h.stores.set('velocity-nettools-app:https://example.test/another/app/:old',new Map());
  h.stores.set('velocity-nettools-app:https://example.test/NetTools/app/:old',new Map());
  await h.event('install');
  await h.event('activate');
  assert.equal(h.claimed,0);
  assert.equal(h.stores.has('learning-centre'),true);
  assert.equal(h.stores.has('velocity-nettools-app:https://example.test/another/app/:old'),true);
  assert.equal(h.stores.has('velocity-nettools-app:https://example.test/NetTools/app/:old'),true);
});


test('missing known assets return 503 without mixing a newer network shell',async()=>{
  const h=harness(); await h.event('install');
  [...h.stores.values()][0].delete('https://example.test/NetTools/app/engine.wasm');
  const fetched=h.fetched;
  const response=await h.fetch('https://example.test/NetTools/app/engine.wasm');
  assert.equal(response.status,503); assert.equal(h.fetched,fetched);
});

test('activation requires unanimous votes and simultaneous requests deduplicate',async()=>{
  const h=harness({votes:[true,true]}); await h.event('install');
  await Promise.all([h.event('message',{data:{type:'ACTIVATE'}}),h.event('message',{data:{type:'ACTIVATE'}})]);
  assert.equal(h.skipped,1);
  assert.equal(h.notices.filter(x=>x.type==='UPDATE_QUERY').length,2);
});

for(const votes of [[true,false],[true,null]]) test('dirty or unresponsive windows block activation '+votes,async()=>{
  const h=harness({votes}); await h.event('install');
  const messages=[];
  await h.event('message',{data:{type:'ACTIVATE'},source:{postMessage(message){messages.push(message)}}});
  assert.equal(h.skipped,0);
  assert.equal(messages.at(-1).type,'UPDATE_BLOCKED');
});

test('outside-scope windows do not vote and a window opened during voting blocks activation',async()=>{
  const h=harness({votes:[true]}); await h.event('install');
  h.windows.push({id:'learning',url:'https://example.test/NetTools/',postMessage(){throw new Error('Must not query learning centre')}});
  await h.event('message',{data:{type:'ACTIVATE'}});
  assert.equal(h.skipped,1);
  const changed=harness({votes:[true]}); await changed.event('install');
  const original=changed.windows[0].postMessage;
  changed.windows[0].postMessage=message=>{
    if(message.type==='UPDATE_QUERY') changed.windows.push({id:'new',url:'https://example.test/NetTools/app/',postMessage(){}});
    original(message);
  };
  await changed.event('message',{data:{type:'ACTIVATE'}});
  assert.equal(changed.skipped,0);
});

test('wrong nonce and unrecognised vote source cannot consent for another window',async()=>{
  const h=harness({votes:[null]}); await h.event('install');
  h.windows[0].postMessage=message=>{
    if(message.type==='UPDATE_QUERY') {
      void h.event('message',{data:{type:'UPDATE_VOTE',nonce:'wrong',ready:true},source:{id:'0'}});
      void h.event('message',{data:{type:'UPDATE_VOTE',nonce:message.nonce,ready:true},source:{id:'outsider'}});
    }
  };
  await h.event('message',{data:{type:'ACTIVATE'}});
  assert.equal(h.skipped,0);
});


test('install rejects a mismatched shell and deletes its candidate cache',async()=>{
  const h=harness({corrupt:true});
  await assert.rejects(h.event('install'),/version|integrity|hash/i);
  assert.equal(h.stores.size,0);
});

test('repair validates the whole shell before restoring missing cached assets',async()=>{
  const h=harness(); await h.event('install');
  const cache=[...h.stores.values()][0];
  cache.delete('https://example.test/NetTools/app/engine.wasm');
  const before=cache.size;
  let reply;
  h.setCorrupt(true);
  await h.event('message',{data:{type:'REPAIR'},ports:[{postMessage(value){reply=value}}]});
  assert.equal(reply.ready,false); assert.match(reply.error,/updates/i);
  assert.equal(cache.size,before);
  h.setCorrupt(false);
  await h.event('message',{data:{type:'REPAIR'},ports:[{postMessage(value){reply=value}}]});
  assert.equal(reply.ready,true); assert.equal((await h.status()).ready,true);
});


test('evicted navigation repairs only a fully verified same-version shell',async()=>{
  const h=harness(); await h.event('install');
  const cache=[...h.stores.values()][0]; cache.delete('https://example.test/NetTools/app/index.html');
  const response=await h.fetch('https://example.test/NetTools/app/','navigate');
  assert.equal(response.status,200);
  assert.equal(await response.text(),readFileSync(join(dirname(sourcePath),'index.html'),'utf8'));
  assert.equal((await h.status()).ready,true);
});

test('navigation version mismatch offers explicit recovery without deleting plans or caches',async()=>{
  const h=harness(); await h.event('install');
  const cache=[...h.stores.values()][0]; cache.delete('https://example.test/NetTools/app/index.html');
  h.setCorrupt(true);
  const response=await h.fetch('https://example.test/NetTools/app/','navigate');
  assert.equal(response.status,503);
  assert.match(response.headers.get('content-type'),/text\/html/);
  const html=await response.text();
  assert.match(html,/Retry online/); assert.match(html,/Reset installation/);
  assert.doesNotThrow(()=>new vm.Script(html.match(/<script>([\s\S]*?)<\/script>/)[1]));
  assert.match(html,/unregister/); assert.doesNotMatch(html,/caches\.delete|localStorage\.clear|indexedDB\.delete/);
  assert.equal(cache.has('https://example.test/NetTools/app/index.html'),false);
});


test('navigation detects an evicted module even when index remains cached',async()=>{
  const h=harness(); await h.event('install');
  const cache=[...h.stores.values()][0]; cache.delete('https://example.test/NetTools/app/app.mjs');
  h.setCorrupt(true);
  const response=await h.fetch('https://example.test/NetTools/app/','navigate');
  assert.equal(response.status,503); assert.match(await response.text(),/Reset installation/);
  h.setCorrupt(false);
  const repaired=await h.fetch('https://example.test/NetTools/app/','navigate');
  assert.equal(repaired.status,200); assert.equal((await h.status()).ready,true);
});

test('activation preserves all older caches while any app window exists',async()=>{
  const h=harness({votes:[false]}); await h.event('install');
  const prefix='velocity-nettools-app:https://example.test/NetTools/app/:';
  for(const version of ['oldest','older','old']) h.stores.set(prefix+version,new Map());
  await h.event('activate');
  for(const version of ['oldest','older','old']) assert.equal(h.stores.has(prefix+version),true);
});
