'use strict';
// Real four-owner game world -> SQLite online backup -> real native C++ bridge
// -> authenticated localhost browser HTTP. No synthetic cargo or mock database.
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const os=require('node:os');
const path=require('node:path');
const {execFileSync}=require('node:child_process');
const {account}=require('../scripts/shipping_auth');
const {createServer}=require('../scripts/shipping_live_server');
function getArg(name){const i=process.argv.indexOf(name);return i<0?'':process.argv[i+1]||'';}
test('authenticated real-world shipment status enforces participant visibility',async t=>{
 const producer=getArg('--producer'),bridge=getArg('--bridge');
 assert.ok(fs.existsSync(producer)&&fs.existsSync(bridge),'native tests must be built first');
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'sg-real-shipping-'));
 t.after(()=>fs.rmSync(dir,{recursive:true,force:true}));
 const db=path.join(dir,'finished-world.sqlite');
 execFileSync(producer,[],{env:{...process.env,SG_SHIPPING_STATUS_FIXTURE:db,SG_SHIPPING_RUN_ID:"auth_integrate"},timeout:30000});
 assert.ok(fs.statSync(db).size>10000,'physical world backup missing');
 const users=[await account('owner-seven',7,'test-seven-secret-pass'),await account('carrier-eight',8,'test-eight-secret-pass'),await account('receiver-ten',10,'test-ten-secret-pass'),await account('stranger-eleven',11,'test-eleven-secret-pass')];
 const srv=createServer({database:db,accounts:users,bridge});t.after(()=>srv.stop());
 const origin=await srv.listen();
 async function view(username,password){let response=await fetch(origin+'/api/login',{method:'POST',headers:{Origin:origin,'Content-Type':'application/json'},body:JSON.stringify({username,password})});
  assert.equal(response.status,200);const cookie=response.headers.get('set-cookie').split(';')[0];
  response=await fetch(origin+'/api/shipments',{headers:{Cookie:cookie},cache:'no-store'});
  assert.equal(response.status,200);return (await response.json()).shipments;
 }
 const source=await view('owner-seven','test-seven-secret-pass');
 assert.equal(source.length,2);assert.ok(source.every(x=>x.settled&&x.role==='source'));
 const carrier=await view('carrier-eight','test-eight-secret-pass');
 assert.equal(carrier.length,2);assert.ok(carrier.every(x=>x.role==='carrier'));
 const receiver=await view('receiver-ten','test-ten-secret-pass');
 assert.equal(receiver.length,1);assert.equal(receiver[0].phase,'redirected_settled');
 const stranger=await view('stranger-eleven','test-eleven-secret-pass');assert.deepEqual(stranger,[]);
});


test('transport identities authorize real canonical host booking consent, dispatch and loading',async t=>{
 const producer=getArg('--producer'),bridge=getArg('--bridge');
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'sg-live-consign-'));t.after(()=>fs.rmSync(dir,{recursive:true,force:true}));
 const db=path.join(dir,'pending-world.sqlite');
 execFileSync(producer,[],{env:{...process.env,SG_SHIPPING_PENDING_FIXTURE:db,SG_SHIPPING_RUN_ID:"pending_integrate"},timeout:30000});
 const accounts=[await account('producer-owner',7,'producer-private-password'),await account('freight-carrier',8,'carrier-private-password'),await account('receiving-owner',9,'receiver-private-password')];
 const live=createServer({database:db,accounts,bridge,writer:{seed:5153,region:'world-return-net',colony:'home',families:['east','west']}});
 t.after(()=>live.stop());const url=await live.listen();
 async function login(username,password){const r=await fetch(url+'/api/login',{method:'POST',headers:{Origin:url,'Content-Type':'application/json'},body:JSON.stringify({username,password})});
  assert.equal(r.status,200);const session=await r.json();assert.equal(session.writes,true);return {cookie:r.headers.get('set-cookie').split(';')[0],csrf:session.csrf};}
 const source=await login('producer-owner','producer-private-password'),carrier=await login('freight-carrier','carrier-private-password'),receiver=await login('receiving-owner','receiver-private-password');
 // A second native host cannot create another writer against this DB.
 const second=createServer({database:db,accounts,bridge,writer:{seed:5153,region:'world-return-net',colony:'home',families:['east','west']}});
 await assert.rejects(second.listen(),/world already has a shipping writer|native shipping host|Native shipping|refused|exited/i);
 const id='frames-third-party';
 async function action(user,method,body){const r=await fetch(url+'/api/shipment/'+method,{method:'POST',headers:{Cookie:user.cookie,Origin:url,'Content-Type':'application/json','X-SG-CSRF':user.csrf},body:JSON.stringify({id,...body})});return {code:r.status,data:await r.json()};}
 async function state(user){const r=await fetch(url+'/api/shipments',{headers:{Cookie:user.cookie}});assert.equal(r.status,200);const p=(await r.json()).shipments.find(x=>x.id===id);return p;}
 assert.equal((await state(source)).phase,'awaiting_approval');
 assert.equal((await action(source,'approve',{role:1,playerId:8})).code,400,'client cannot declare owner');
 assert.equal((await action(carrier,'approve',{role:1})).code,403,'carrier cannot approve warehouse owner role');
 assert.equal((await action(source,'approve',{role:1})).code,200);
 assert.equal((await action(carrier,'approve',{role:2})).code,200);
 assert.equal((await action(receiver,'approve',{role:3})).code,200);
 assert.equal((await state(source)).phase,'approved_awaiting_dispatch');
 assert.equal((await action(source,'dispatch',{dispatchId:'host-dispatch-frames'})).code,403);
 assert.equal((await action(carrier,'dispatch',{dispatchId:'host-dispatch-frames'})).code,200);
 assert.equal((await state(carrier)).phase,'dispatched_awaiting_loading');
 assert.equal((await action(receiver,'load',{})).code,403,'recipient cannot load carrier cargo');
 assert.equal((await action(carrier,'load',{})).code,200);
 const cargo=await state(carrier);assert.equal(cargo.phase,'in_transit');assert.equal(cargo.loaded,true);
 assert.equal((await action(carrier,'load',{})).code,200,'exact retry remains idempotent');
 await live.stop();
 const readonly=createServer({database:db,accounts,bridge});t.after(()=>readonly.stop());const readUrl=await readonly.listen();
 const r=await fetch(readUrl+'/api/login',{method:'POST',headers:{Origin:readUrl,'Content-Type':'application/json'},body:JSON.stringify({username:'freight-carrier',password:'carrier-private-password'})});
 assert.equal(r.status,200);const newCookie=r.headers.get('set-cookie').split(';')[0];
 const after=await fetch(readUrl+'/api/shipments',{headers:{Cookie:newCookie}});assert.equal(after.status,200);
 assert.equal((await after.json()).shipments.find(x=>x.id===id).phase,'in_transit','restart lost the real transfer');
});
