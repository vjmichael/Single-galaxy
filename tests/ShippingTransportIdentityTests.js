'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const os=require('node:os');
const path=require('node:path');
const http=require('node:http');
const {account,validate,verify}=require('../scripts/shipping_auth');
const {createServer}=require('../scripts/shipping_live_server');
const {writeFileSync}=fs;

test('credential resolver: passwords and owner identities are never client-declared',async()=>{
 const a=await account('pilot-seven',7,'this-is-a-unique-long-password');
 const b=await account('pilot-eight',8,'another-unique-long-password');
 assert.equal((await verify([a,b],'pilot-seven','this-is-a-unique-long-password')).playerId,7);
 assert.equal(await verify([a,b],'pilot-seven','wrong-password'),null);
 assert.equal(await verify([a,b],'made-up-user','wrong-password'),null);
 assert.throws(()=>validate({version:1,accounts:[a,{...b,playerId:7}]}),/duplicate/);
 assert.throws(()=>validate({version:1,accounts:[{...a,hash:'not-a-hash'}]}),/Invalid/);
});

test('HTTP loopback session resolves to fixed host-side player; rejects CSRF and owner spoofing',async t=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'sg-shipping-session-'));
 t.after(()=>fs.rmSync(dir,{recursive:true,force:true}));
 const database=path.join(dir,'world.db');writeFileSync(database,'stub');
 const bridge=path.join(dir,'bridge.js');
 writeFileSync(bridge,`#!/usr/bin/env node
let rest='';process.stdin.setEncoding('utf8');process.stdin.on('data',data=>{rest+=data;while(rest.includes('\\n')){const i=rest.indexOf('\\n');const line=rest.slice(0,i);rest=rest.slice(i+1);console.log(JSON.stringify(line==='CAPS'?{ok:true,writes:false}:{ok:true,shipments:[{trustedPrincipal:Number(line.slice(5))}]}));}});
`,{mode:0o700});
 const users=[await account('source-player',7,'the-source-test-password'),await account('carrier-player',8,'the-carrier-test-password')];
 const live=createServer({database,accounts:users,bridge});t.after(()=>live.stop());
 const origin=await live.listen();
 const get=async(route,cookie='',opts={})=>fetch(origin+route,{headers:cookie?{Cookie:cookie}:{},...opts});
 let result=await get('/api/shipments');assert.equal(result.status,401);
 result=await get('/api/session');assert.equal(result.status,401);
 const forgedHost=await new Promise((resolve,reject)=>{
   const u=new URL(origin),r=http.request({host:u.hostname,port:Number(u.port),path:'/api/shipments',headers:{Host:'evil.site'}},res=>{res.resume();res.on('end',()=>resolve(res.statusCode));});r.on('error',reject);r.end();});
 assert.equal(forgedHost,403);
 result=await get('/api/login','',{method:'POST',headers:{'Content-Type':'application/json',Origin:'http://evil.site'},body:JSON.stringify({username:'source-player',password:'the-source-test-password'})});assert.equal(result.status,403);
 result=await get('/api/login','',{method:'POST',headers:{'Content-Type':'application/json',Origin:origin},body:JSON.stringify({username:'source-player',password:'the-source-test-password',playerId:8})});assert.equal(result.status,400);
 result=await get('/api/login','',{method:'POST',headers:{'Content-Type':'application/json',Origin:origin},body:JSON.stringify({username:'source-player',password:'wrong-password'})});assert.equal(result.status,401);
 result=await get('/api/login','',{method:'POST',headers:{'Content-Type':'application/json',Origin:origin},body:JSON.stringify({username:'source-player',password:'the-source-test-password'})});assert.equal(result.status,200);
 const owner=await result.json(),cookie=result.headers.get('set-cookie').split(';')[0];
 assert.equal(owner.playerId,7);assert.match(result.headers.get('set-cookie'),/HttpOnly; SameSite=Strict/);
 result=await get('/api/shipments?playerId=8',cookie);assert.equal(result.status,200);
 assert.equal((await result.json()).shipments[0].trustedPrincipal,7);
 result=await get('/api/logout',cookie,{method:'POST',headers:{Cookie:cookie,'Content-Type':'application/json',Origin:origin,'X-SG-CSRF':'wrong'},body:'{}'});assert.equal(result.status,403);
 result=await get('/api/logout',cookie,{method:'POST',headers:{Cookie:cookie,'Content-Type':'application/json',Origin:origin,'X-SG-CSRF':owner.csrf},body:'{}'});assert.equal(result.status,200);
 result=await get('/api/shipments',cookie);assert.equal(result.status,401);
 result=await get('/api/login','',{method:'POST',headers:{'Content-Type':'application/json',Origin:origin},body:JSON.stringify({username:'carrier-player',password:'the-carrier-test-password'})});assert.equal(result.status,200);
 const cookie2=result.headers.get('set-cookie').split(';')[0];
 result=await get('/api/shipments',cookie2);assert.equal((await result.json()).shipments[0].trustedPrincipal,8);
});
