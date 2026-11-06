'use strict';
// SHIP-STATUS-07E: real native-protocol parser and interactive DOM controls.
const assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path');
const security=require('../viewer/security-desk.js');
const parsed=security.parse(
 'COLONY|1|22|SECURITY_CALL_INBOX|9223372036854775807|20|8');
assert.deepEqual(parsed,{
 tick:22,type:'SECURITY_CALL_INBOX',
 request:'9223372036854775807',ship:20,caller:8
});
assert.equal(security.parse(
 'COLONY|1|22|SECURITY_CALL_PENALTY|3|34|1').review,true);
assert.equal(security.parse(
 'COLONY|1|22|SECURITY_CALL_PENALTY|9007199254740993|34|1').points,
 '9007199254740993');
assert.equal(security.parse(
 'COLONY|1|22|SECURITY_CALL_SANCTION|20|1|24|0').points,'1');
assert.equal(security.parse(
 'COLONY|1|22|SECURITY_CALL_SANCTION|20|0|0|0').points,'0');
assert.equal(security.parse(
 'COLONY|1|22|SECURITY_CALL_COMPLAINT|9000|8|3').caller,8);
for(const forged of [
 'COLONY|1|22|SECURITY_CALL_INBOX|01|20|8',
 'COLONY|1|22|SECURITY_CALL_INBOX|9223372036854775808|20|8',
 'COLONY|1|22|SECURITY_CALL_INBOX|1|20|0',
 'COLONY|1|22|SECURITY_CALL_INBOX|1|20|8|EXTRA',
 'COLONY|1|22|SECURITY_CALL_PENALTY|0|34|0',
 'COLONY|1|22|SECURITY_CALL_PENALTY|2|34|1',
 'COLONY|1|22|SECURITY_CALL_COMPLAINT|9000|8|2',
 'COLONY|1|22|SECURITY_CALL_SANCTION|20|1|0|0',
 'COLONY|1|22|SECURITY_CALL_REPLY|WEAPON|ANY',
 'COLONY|1|22|SECURITY_RESULT|SECURITY_CALL|ACCEPTED|0|0',
 'COLONY|1|22|SECURITY_RESULT|SECURITY_ARREST|ACCEPTED|31|0'
 ])assert.throws(()=>security.parse(forged),undefined,forged);
class OptionNode{constructor(text,value){this.text=text;this.value=value;}}
class Element{
 constructor(){this.textContent='';this.value='';this.children=[];
  this.options=[];this.listeners={};this.disabled=false;this.style={};}
 replaceChildren(...nodes){
  this.children=nodes;this.options=nodes.filter(n=>n instanceof OptionNode);
  if(this.options.length&&!this.options.some(o=>o.value===this.value))
   this.value=this.options[0].value;
 }
 append(...nodes){this.children.push(...nodes);}
 addEventListener(event,fn){this.listeners[event]=fn;}
}
const elements=new Map();
global.Option=OptionNode;
global.document={
 getElementById(id){
  if(!elements.has(id))elements.set(id,new Element());
  return elements.get(id);
 },createElement:()=>new Element()
};
const messages=[];
global.window={GalaxyColonyCommand:(name,args)=>{
 messages.push({name,args});return Promise.resolve(true);
}};
delete require.cache[require.resolve('../viewer/security-desk.js')];
const desk=require('../viewer/security-desk.js');
const el=id=>global.document.getElementById(id);
const event=raw=>assert.equal(desk.accept(raw),true);
event('COLONY|1|22|SECURITY_STATUS|5|5|1|0|2');
event('COLONY|1|22|SECURITY_CONTACT|20|1|434f4c4f4e595f48554c4c');
el('securityContact').value='20';
el('securityContact').listeners.change();
assert.equal(el('securityCallShip').disabled,false);
el('securityCallShip').listeners.click();
assert.deepEqual(messages.pop(),
 {name:'colonySecurityCall',args:{ship:20}});
event('COLONY|1|22|SECURITY_CALL_INBOX|9223372036854775807|20|8');
el('securityCallInbox').value='9223372036854775807';
el('securityCallInbox').listeners.change();
assert.equal(el('securityIdentifyCaller').disabled,false);
el('securityIdentifyCaller').listeners.click();
assert.deepEqual(messages.pop(),{name:'colonySecurityCallReply',args:{
 request:'9223372036854775807',choice:'identify'
}});
el('securityDeclineCaller').listeners.click();
assert.deepEqual(messages.pop(),{name:'colonySecurityCallReply',args:{
 request:'9223372036854775807',choice:'decline'
}});
event('COLONY|1|22|SECURITY_CALL_SANCTION|20|1|24|0');
assert.match(el('securityDeskFeedback').textContent,/BLOCKED/);
event('COLONY|1|22|SECURITY_RESULT|SECURITY_CALL|DENIED|0|0');
assert.match(el('securityDeskFeedback').textContent,/BLOCKED/);
assert.equal(el('securityCallShip').disabled,true);
assert.match(el('securityCommsPenalty').textContent,/score 1/);
event('COLONY|1|22|SECURITY_CALL_SANCTION|20|3|34|1');
assert.match(el('securityCommsPenalty').textContent,/law-enforcement review eligible/);
event('COLONY|1|22|SECURITY_CALL_COMPLAINT|9000|8|3');
assert.match(el('securityCommsComplaints').children[0].textContent,
 /not a conviction/);
event('COLONY|1|22|SECURITY_CALL_INBOX_RESET');
assert.equal(el('securityIdentifyCaller').disabled,true);
event('COLONY|1|35|SECURITY_STATUS|5|5|1|0|2');
assert.equal(el('securityCallShip').disabled,false);
desk.reset();
assert.equal(el('securityCallShip').disabled,true);
assert.match(el('securityCommsPenalty').textContent,/No communications abuse penalty/);
const html=fs.readFileSync(
 path.join(__dirname,'../viewer/interactive.html'),'utf8');
for(const id of ['securityCallShip','securityCallInbox','securityIdentifyCaller',
 'securityDeclineCaller','securityCommsPenalty','securityCommsComplaints'])
 assert.match(html,new RegExp('id="'+id+'"'));
delete global.Option;delete global.document;delete global.window;
console.log('PASS SHIP-STATUS-07E: authenticated hail identity/decline UI, repeated-call ban, native law-review-only complaint and logout privacy');
