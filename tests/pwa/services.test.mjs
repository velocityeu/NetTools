import test from 'node:test';
import assert from 'node:assert/strict';
import {isIP} from 'node:net';

const plans = await import('../../website-preview/dist/app/plans.mjs').catch(() => ({}));
const ip = await import('../../website-preview/dist/app/public-ip.mjs').catch(() => ({}));
const sample = () => ({version:1,id:'test-plan',name:'Office',type:'vlsm',family:'ipv4',input:{parent:'192.0.2.0/24'},requirements:[{id:'r1',name:'LAN',sequence:'0',kind:'lan',quantity:'50',assigned:'',pinned:false}],reservations:[],state:'draft',updatedAt:'2026-09-13T12:00:00.000Z'});
const memory = () => ({data:new Map(),getItem(k){return this.data.get(k) ?? null;},setItem(k,v){this.data.set(k,v);}});
const validateAddress = async (value,family) => {if(isIP(value)!==(family==='ipv4'?4:6)) throw Error('Wrong address family');return value;};
const json = value => new Response(JSON.stringify({ip:value}),{headers:{'content-type':'application/json'}});
const tick = () => new Promise(resolve=>setImmediate(resolve));

test('plan round trip retains exact integers and returns independent editable objects',()=>{
 const p=sample(); p.input.index='340282366920938463463374607431768211455';
 const parsed=plans.importPlan(JSON.stringify(p)); assert.deepEqual(parsed,p); parsed.requirements[0].name='Changed'; assert.equal(p.requirements[0].name,'LAN');
});
test('plan schema rejects unsupported semantics, duplicates and oversized or numeric quantities',()=>{
 assert.equal(typeof plans.validatePlan,'function');
 for(const mutate of [p=>p.version=2,p=>p.unknown=true,p=>p.input.future=true,p=>p.requirements[0].quantity=50,p=>p.requirements[0].quantity='1e20',p=>p.requirements.push({...p.requirements[0]}),p=>p.name='x'.repeat(201),p=>p.requirements[0].pinned='yes',p=>p.input.prefix='129',p=>p.reservations=[{id:'a',name:'Reserve',cidr:'x',extra:1}]]){
  const p=sample();mutate(p);assert.throws(()=>plans.validatePlan(p));
 }
 assert.throws(()=>plans.importPlan('{broken'));
});
test('explicit saves replace one ID and failed writes leave previous storage intact',()=>{
 const s=memory();assert.deepEqual(plans.loadPlans(s),[]);plans.savePlan(sample(),s);
 const changed=sample();changed.name='Updated';plans.savePlan(changed,s);assert.equal(plans.loadPlans(s).length,1);assert.equal(plans.loadPlans(s)[0].name,'Updated');
 s.setItem=()=>{throw Error('Quota exceeded');};assert.throws(()=>plans.savePlan(sample(),s),/Quota/);assert.equal(plans.loadPlans(s)[0].name,'Updated');
});
test('import never writes and deletion preserves other plans; corrupt library is not overwritten',()=>{
 const s=memory();plans.savePlan(sample(),s);plans.savePlan({...sample(),id:'second'},s);plans.deletePlan('test-plan',s);assert.deepEqual(plans.loadPlans(s).map(p=>p.id),['second']);
 const key=[...s.data.keys()][0];s.data.set(key,'{}');assert.throws(()=>plans.savePlan(sample(),s));assert.equal(s.data.get(key),'{}');
});
test('launch checks default off and denied preference persistence throws',()=>{
 const s=memory();assert.equal(ip.getLaunchPreference(s),false);ip.setLaunchPreference(true,s);assert.equal(ip.getLaunchPreference(s),true);ip.setLaunchPreference(false,s);assert.equal(ip.getLaunchPreference(s),false);
 s.setItem=()=>{throw Error('Denied');};assert.throws(()=>ip.setLaunchPreference(true,s),/Denied/);
});
test('public IP reports independent family completion and uses private credential-free requests',async()=>{
 let resolve6;const snapshots=[];
 const c=new ip.PublicIpController({validateAddress,onChange:s=>snapshots.push(s),fetchImpl:(url,opts)=>{
  assert.equal(opts.credentials,'omit');assert.equal(opts.mode,'cors');assert.equal(opts.cache,'no-store');assert.equal(opts.redirect,'error');
  if(url==='https://api.ipify.org?format=json')return Promise.resolve(json('192.0.2.1'));
  assert.equal(url,'https://api6.ipify.org?format=json');return new Promise(resolve=>resolve6=resolve);
 }});
 const done=c.check();await tick();assert.equal(c.states.ipv4.status,'verified');assert.equal(c.states.ipv6.status,'checking');resolve6(json('2001:db8::1'));await done;
 assert.equal(c.states.ipv6.address,'2001:db8::1');assert.ok(c.states.ipv4.checkedAt);assert.ok(snapshots.length>=3);
});
test('failed refresh preserves observation as previous and wrong-family JSON is rejected',async()=>{
 let refresh=false;const c=new ip.PublicIpController({validateAddress,fetchImpl:url=>Promise.resolve(json(refresh?'2001:db8::2':url.includes('api6')?'2001:db8::1':'192.0.2.1'))});
 await c.check();refresh=true;await c.check();assert.equal(c.states.ipv4.status,'failed');assert.equal(c.states.ipv4.previous.address,'192.0.2.1');assert.equal(c.states.ipv4.address,'');assert.equal(c.states.ipv6.status,'verified');
});
test('deadline completes even when transport ignores abort',async()=>{
 const c=new ip.PublicIpController({validateAddress,timeoutMs:15,fetchImpl:()=>new Promise(()=>{})});await c.check();assert.equal(c.states.ipv4.status,'failed');assert.match(c.states.ipv6.error,/timed out/i);
});
test('cancel suppresses late responses and permits a new revision',async()=>{
 const late=[];let initial=true;const c=new ip.PublicIpController({validateAddress,fetchImpl:url=>initial?new Promise(resolve=>late.push(resolve)):Promise.resolve(json(url.includes('api6')?'2001:db8::2':'192.0.2.2'))});
 const old=c.check();await tick();c.cancel();initial=false;await c.check();for(const resolve of late)resolve(json('192.0.2.99'));await old;assert.equal(c.states.ipv4.address,'192.0.2.2');assert.equal(c.states.ipv6.address,'2001:db8::2');
});
test('malformed JSON and streamed oversize bodies are rejected',async()=>{
 for(const response of [()=>new Response('{broken'),()=>new Response(JSON.stringify({ip:'192.0.2.1',extra:'x'})),()=>new Response(new ReadableStream({start(controller){controller.enqueue(new Uint8Array(1025));controller.close();}}))]){
  const c=new ip.PublicIpController({validateAddress,fetchImpl:async()=>response()});await c.check();assert.equal(c.states.ipv4.status,'failed');assert.equal(c.states.ipv6.status,'failed');
 }
});


test('explicit null collections cannot be silently normalized away',()=>{
 for(const field of ['requirements','reservations']){const p=sample();p[field]=null;assert.throws(()=>plans.validatePlan(p));}
});

test('overlapping check returns existing operation; stalled body obeys deadline',async()=>{
 let requests=0;
 const c=new ip.PublicIpController({validateAddress,timeoutMs:15,fetchImpl:async()=>{requests++;return new Response(new ReadableStream({start(){}}));}});
 const first=c.check();assert.equal(c.check(),first);await first;assert.equal(requests,2);assert.equal(c.states.ipv4.status,'failed');assert.match(c.states.ipv4.error,/timed out/i);
});

test('timeout includes async validation and ignores its late completion',async()=>{
 const resolveValidators=[];
 const c=new ip.PublicIpController({timeoutMs:15,validateAddress:()=>new Promise(resolve=>resolveValidators.push(resolve)),fetchImpl:async()=>json('192.0.2.1')});
 await c.check();for(const resolve of resolveValidators)resolve('192.0.2.1');await tick();assert.equal(c.states.ipv4.status,'failed');assert.equal(c.states.ipv4.address,'');
});
