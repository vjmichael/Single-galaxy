'use strict';
// SHIP-STATUS-07H: exercise the actual standalone security-only client
// through a controlled DOM/EventSource harness, not a mock client rewrite.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');

class Element{
  constructor(){
    this.value='';this.textContent='';this.hidden=false;this.listeners={};
  }
  addEventListener(name,fn){this.listeners[name]=fn;}
}
const nodes=new Map(),streams=[],requests=[],accepted=[];
let resets=0;
const get=id=>{
  if(!nodes.has(id))nodes.set(id,new Element());
  return nodes.get(id);
};
class FakeEvents{
  constructor(url,options){
    this.url=url;this.options=options;this.listeners={};
    this.closed=false;streams.push(this);
  }
  addEventListener(name,fn){this.listeners[name]=fn;}
  emit(name,data=''){assert.ok(this.listeners[name],name);
    this.listeners[name]({data});}
  close(){this.closed=true;}
}
const scripted=fs.readFileSync(
  path.join(__dirname,'../viewer/security-only.js'),'utf8');
const context={
  document:{getElementById:get},EventSource:FakeEvents,
  window:{GalaxySecurityDesk:{
    reset(){resets++;},
    accept(frame){accepted.push(frame);return true;}
  }},
  atob:encoded=>Buffer.from(encoded,'base64').toString('latin1'),
  fetch:async(url,options={})=>{
    requests.push({url,options});
    if(url==='/session')return {
      ok:true,json:async()=>({token:'owner-8-csrf',commands:[
        'colonySecurityRefresh','colonySecurityCall',
        'colonySecurityCallReply']})
    };
    if(url==='/command')return {
      ok:true,json:async()=>({status:'queued_not_committed'})
    };
    throw Error('Unexpected network request '+url);
  }
};
const enc=text=>Buffer.from(text,'ascii').toString('base64');
async function run(){
  vm.runInNewContext(scripted,context,{filename:'security-only.js'});
  // Bootstrap is async and subscribes only after trusted /session.
  await new Promise(resolve=>setImmediate(resolve));
  assert.equal(streams.length,1);
  const stream=streams[0];
  assert.equal(stream.url,'/events');
  assert.equal(stream.options.withCredentials,true);
  assert.equal(get('securityOwnerPanel').hidden,false);
  assert.equal(get('securityLogin').hidden,true);
  const count=resets;
  stream.emit('colony',enc('COLONY|1|25|SECURITY_STATUS|8|8|1|0|2'));
  assert.equal(accepted.length,1);
  // No global world frames, foreign private wrappers or fake native cases.
  stream.emit('colony',enc('SGVIEW|1|25|HULL|7'));
  stream.emit('colony',enc('PRIVATE|7|COLONY|1|25|SECURITY_CASE|FORGED'));
  assert.equal(accepted.length,1);
  stream.emit('resync','Native security refresh required');
  assert.equal(resets,count+1);
  assert.match(get('securityDeskFeedback').textContent,/reloading saved/);
  assert.equal(get('securityOwnerPanel').hidden,false,
    'a recoverable overflow is not a sign-out');
  stream.emit('colony',enc('COLONY|1|26|SECURITY_CALL_INBOX_RESET'));
  assert.equal(accepted.length,2);
  assert.equal(await context.window.GalaxyColonyCommand(
    'colonyFireShip',{target:7}),false);
  assert.equal(await context.window.GalaxyColonyCommand(
    'colonySecurityRefresh'),true);
  const command=requests.filter(x=>x.url==='/command');
  assert.equal(command.length,1);
  assert.equal(JSON.parse(command[0].options.body).action,
    'colonySecurityRefresh');
  stream.emit('failed','Session expired');
  assert.equal(stream.closed,true);
  assert.equal(get('securityOwnerPanel').hidden,true);
  assert.equal(get('securityPassword').value,'');
  assert.equal(await context.window.GalaxyColonyCommand(
    'colonySecurityCall',{ship:7}),false);
  console.log('PASS SHIP-STATUS-07H: real security-only JS rejects world frames, '+
    'resync clears stale UI, native refresh resumes and revoked session closes');
}
run().catch(error=>{console.error(error);process.exitCode=1;});
