import test from 'node:test';
import assert from 'node:assert/strict';
import {existsSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const path = new URL('../../website-preview/dist/app/engine.mjs', import.meta.url);
test('browser arithmetic engine exists',()=>assert.ok(existsSync(path),'shared WASM engine has not been built'));
if(existsSync(path)) {
 const {default:create}=await import(path); const engine=await create();
 const run=(op,data)=>{const r=JSON.parse(engine.execute(JSON.stringify({op,...data}))); assert.equal(r.ok,true,r.error); return r.result;};
 test('independent oracle covers every IPv4 and IPv6 prefix',()=>{const fixtures=JSON.parse(execFileSync(process.platform==='win32'?'python':'python3',[fileURLToPath(new URL('./arithmetic_fixtures.py',import.meta.url))],{encoding:'utf8'}));for(const f of fixtures){const r=run('calculate',{address:f.address});for(const k of ['network','last','total'])assert.equal(r[k],f[k],f.address+' '+k);}});
 test('IPv4 network and usable endpoints',()=>{const r=run('calculate',{address:'192.168.10.42/26'});assert.equal(r.network,'192.168.10.0/26');assert.equal(r.total,'64');assert.equal(r.firstHost,'192.168.10.1');assert.equal(r.lastHost,'192.168.10.62');assert.equal(r.broadcast,'192.168.10.63');});
 test('boundary counts remain exact',()=>{for(const [address,total] of [['0.0.0.0/0','4294967296'],['192.0.2.10/31','2'],['192.0.2.10/32','1'],['2001:db8::1/64','18446744073709551616'],['::/0','340282366920938463463374607431768211456'],['ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff/128','1']])assert.equal(run('calculate',{address}).total,total);});
 test('CIDR wins over prefix and mapped IPv6 stays IPv6',()=>{assert.equal(run('calculate',{address:'192.0.2.1/24',prefix:'8'}).network,'192.0.2.0/24');assert.equal(run('calculate',{address:'::ffff:192.0.2.1/128'}).family,'ipv6');});
 test('last IPv6 split uses 128-bit index',()=>{const r=run('split',{parent:'::/0',child:'128',index:'340282366920938463463374607431768211455'});assert.equal(r.rows[0],'ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff/128');assert.equal(r.rows.length,1);});
 test('VLSM role capacity, free count and largest block',()=>{const r=run('allocate',{parent:'192.168.10.0/24',requirements:[{id:'a',name:'Office',sequence:'0',kind:'lan',quantity:'50'},{id:'b',name:'Voice',sequence:'1',kind:'lan',quantity:'25'},{id:'c',name:'Link',sequence:'2',kind:'ptp',quantity:'2'}]});assert.deepEqual(r.allocations.map(x=>x.cidr),['192.168.10.0/26','192.168.10.64/27','192.168.10.96/31']);assert.equal(r.free,'158');assert.equal(r.largestFree,'192.168.10.128/25');});
 test('valid existing allocation survives reduced demand',()=>{const r=run('allocate',{parent:'192.168.10.0/24',requirements:[{id:'a',name:'Office',sequence:'0',kind:'lan',quantity:'25',assigned:'192.168.10.0/26'}]});assert.equal(r.allocations[0].cidr,'192.168.10.0/26');assert.equal(r.allocations[0].preserved,true);});
 test('pins and reservations are excluded from new allocations',()=>{const r=run('allocate',{parent:'192.0.2.0/24',requirements:[{id:'a',kind:'lan',quantity:'20',assigned:'192.0.2.64/27',pinned:true}],reservations:[{id:'r',cidr:'192.0.2.0/26'}],reallocate:true});assert.equal(r.allocations[0].cidr,'192.0.2.64/27');assert.equal(r.reserved,'64');assert.equal(r.free,'160');});
 test('sequence rejects overflow and non-decimal input',()=>{for(const sequence of ['18446744073709551616','-1','1x'])assert.equal(JSON.parse(engine.execute(JSON.stringify({op:'allocate',parent:'192.0.2.0/24',requirements:[{id:'a',kind:'lan',quantity:'2',sequence}]}))).ok,false);});
 test('invalid input is an error without terminating engine',()=>{for(const data of [{op:'calculate',address:'1.2.3.4/33'},{op:'calculate',address:'1.2.3.4',prefix:'255.0.255.0'},{op:'allocate',parent:'192.0.2.0/24',requirements:[{id:'a',kind:'lan',quantity:'1',assigned:'192.0.2.1/30',pinned:true}]}])assert.equal(JSON.parse(engine.execute(JSON.stringify(data))).ok,false);assert.equal(run('calculate',{address:'::1/128'}).total,'1');});
}
