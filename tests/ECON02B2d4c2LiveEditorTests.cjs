'use strict';
const assert=require('node:assert/strict');
const fs=require('node:fs');
const os=require('node:os');
const path=require('node:path');
const {spawn,spawnSync}=require('node:child_process');
const args=process.argv.slice(2);
const arg=n=>args[args.indexOf(n)+1];
let root=fs.mkdtempSync(path.join(os.tmpdir(),'sg-live-factory-'));
let server;
async function launch(db){
  server=spawn(process.execPath,[arg('--server'),'--db',db,'--bridge',arg('--bridge')],{stdio:['ignore','pipe','pipe']});
  let output='',error='';server.stderr.on('data',x=>error+=x);
  const url=await new Promise((resolve,reject)=>{
    const timeout=setTimeout(()=>reject(Error('live server launch timeout: '+error)),8000);
    server.once('exit',code=>{clearTimeout(timeout);reject(Error('server quit '+code+': '+error));});
    server.stdout.on('data',c=>{output+=c;const m=output.match(/SG_FACTORY_URL=(https?:\/\/\S+)/);if(m){clearTimeout(timeout);resolve(m[1]);}});
  });
  return url;
}
async function stop(){if(!server)return;const prev=server;server=null;
  if(prev.exitCode===null){prev.kill('SIGTERM');await new Promise(r=>{prev.once('exit',r);setTimeout(r,1000);});}}
async function main(){
  const db=path.join(root,'world.sqlite');
  const fixture=spawnSync(arg('--fixture'),[db],{encoding:'utf8',timeout:10000});
  assert.equal(fixture.status,0,fixture.stderr);
  const layout=fs.readFileSync(arg('--layout'),'utf8');
  let url=await launch(db);
  async function get(name){const r=await fetch(new URL('api/'+name,url));return [r,await r.json()];}
  async function post(name,body,opts={}){
    const dest=new URL('api/'+name,url);
    const headers={'Content-Type':'application/json',Origin:dest.origin,'X-SG-Session':dest.pathname.match(/\/session\/([a-f0-9]{64})\//)[1],...opts.headers};
    const response=await fetch(dest,{method:'POST',headers,body:JSON.stringify(body)});
    return [response,await response.json()];
  }
  let [resp,hello]=await get('hello');assert.equal(resp.status,200);assert(hello.site>0);assert(hello.modulesAvailable>=12);
  // No global path, no ambient browser authentication, no foreign origin.
  resp=await fetch(new URL('/api/hello',url));assert.equal(resp.status,404);
  resp=await fetch(new URL('factory.html',url));assert.equal(resp.status,200);assert((await resp.text()).includes('Authorize plan'));
  resp=await fetch(new URL('factory-live.js',url));assert.equal(resp.status,200);
  assert((await resp.text()).includes('window.SingleGalaxyEditorInstance'));
  [resp]=await post('register',{layout},{headers:{Origin:'https://evil.test'}});assert.equal(resp.status,403);
  [resp]=await post('register',{layout},{headers:{'X-SG-Session':'wrong'}});assert.equal(resp.status,403);
  [resp]=await post('register',{layout:'{"format":"single-galaxy-factory-design-v1"}'});assert.equal(resp.status,400);
  [resp]=await post('register',{layout:layout+'extra'});assert.equal(resp.status,400);
  let result;
  [resp,result]=await post('register',{layout});assert.equal(resp.status,200);assert.equal(result.plan.total,16);
  const id=result.plan.id;
  [resp,result]=await post('register',{layout});assert.equal(resp.status,400);assert.match(result.error,/pending host-authorized/);
  for(let i=1;i<=4;i++){
    [resp,result]=await post('step',{id});assert.equal(resp.status,200);assert.equal(result.event.applied,true);
    assert.equal(result.plan.completed,i);
  }
  [resp,result]=await get('plans');assert.equal(resp.status,200);assert.equal(result.plans[0].completed,4);
  const before=(await get('hello'))[1].modulesAvailable;
  assert(before<hello.modulesAvailable);
  // Simulate service restart; native world+plan must survive in SQLite.
  await stop();url=await launch(db);
  [resp,result]=await get('plans');assert.equal(result.plans[0].completed,4);
  [resp,result]=await post('step',{id});assert.equal(resp.status,200);assert.equal(result.plan.completed,5);
  [resp,result]=await post('cancel',{id});assert.equal(resp.status,200);assert.equal(result.plan.cancelled,true);
  [resp,result]=await post('step',{id});assert.equal(resp.status,400);assert.match(result.error,/cancelled/);
  [resp,result]=await post('register',{layout});assert.equal(resp.status,200);
  assert.equal(result.plan.completed,5);assert(result.plan.id>id);
  console.log('ECON-02B2d4c2 localhost HTTP controls, CSRF, real world construction, restart/cancel PASS');
}
main().then(async()=>{await stop();fs.rmSync(root,{recursive:true,force:true});},async e=>{
  console.error(e);await stop();fs.rmSync(root,{recursive:true,force:true});process.exitCode=1;
});
