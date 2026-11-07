'use strict';
// Actual owner security parser and DOM command bindings for STATUS-07I/07J.
const assert=require('node:assert/strict');
const security=require('../viewer/security-desk.js');
const good='COLONY|1|25|SECURITY_NOTIFICATION|9001|2|9100|22|7|0';
assert.deepEqual(security.parse(good),{
 tick:25,type:'SECURITY_NOTIFICATION',sequence:'9001',
 kind:2,event:'9100',ship:22,peerOwner:7,related:'0'
});
assert.equal(security.parse(
 'COLONY|1|25|SECURITY_NOTIFICATION|9223372036854775807|4|9002|22|0|9001'
).sequence,'9223372036854775807');
assert.equal(security.parse(
 'COLONY|1|25|SECURITY_AGENCY_STATUS|100|101|1').active,true);
assert.equal(security.parse(
 'COLONY|1|25|SECURITY_AGENCY_REFERRAL|7000|5005|7|3|101|102'
).reviewer,102);
for(const forged of [
 'COLONY|1|25|SECURITY_NOTIFICATION|0|2|9100|22|7|0',
 'COLONY|1|25|SECURITY_NOTIFICATION|01|2|9100|22|7|0',
 'COLONY|1|25|SECURITY_NOTIFICATION|9001|8|9100|22|7|0',
 'COLONY|1|25|SECURITY_NOTIFICATION|9001|2|9100|22|0|0',
 'COLONY|1|25|SECURITY_NOTIFICATION|9001|1|9100|22|8|0',
 'COLONY|1|25|SECURITY_NOTIFICATION|9001|7|9100|22|8|0',
 'COLONY|1|25|SECURITY_NOTIFICATION|9001|2|9100|22|7|0|HACK',
 'COLONY|1|25|SECURITY_AGENCY_STATUS|100|100|1',
 'COLONY|1|25|SECURITY_AGENCY_STATUS|100|0|1',
 'COLONY|1|25|SECURITY_AGENCY_REFERRAL|7000|5005|7|2|101|102',
 'COLONY|1|25|SECURITY_AGENCY_REFERRAL|7000|5005|7|3|101|101',
 'COLONY|1|25|SECURITY_RESULT|SECURITY_POLICE_ARREST|ACCEPTED|1|0'
])assert.throws(()=>security.parse(forged),undefined,forged);

class OptionNode{constructor(text,value){this.text=text;this.value=value;}}
class Element{
 constructor(){this.textContent='';this.value='';this.children=[];
  this.options=[];this.listeners={};this.disabled=false;this.style={};}
 replaceChildren(...nodes){
  this.children=nodes;this.options=nodes.filter(x=>x instanceof OptionNode);
  if(this.options.length&&!this.options.some(x=>x.value===this.value))
    this.value=this.options[0].value;
 }
 append(...nodes){this.children.push(...nodes);}
 addEventListener(event,fn){this.listeners[event]=fn;}
}
const elements=new Map(),calls=[];
global.Option=OptionNode;
global.document={
 getElementById(id){
  if(!elements.has(id))elements.set(id,new Element());
  return elements.get(id);
 },createElement:()=>new Element()
};
global.window={GalaxyColonyCommand:(action,args)=>{
  calls.push({action,args});return Promise.resolve(true);
}};
delete require.cache[require.resolve('../viewer/security-desk.js')];
const desk=require('../viewer/security-desk.js');
const el=id=>document.getElementById(id);
const frame=v=>assert.equal(desk.accept(v),true);
frame('COLONY|1|25|SECURITY_STATUS|8|8|1|0|2');
frame('COLONY|1|25|SECURITY_AGENCY_STATUS|0|0|0');
assert.equal(el('securityAgencyEstablish').disabled,false);
el('securityAgencyEstablish').listeners.click();
assert.equal(calls.pop().action,'colonySecurityAgencyEstablish');
frame('COLONY|1|25|SECURITY_AGENCY_STATUS|100|101|1');
frame('COLONY|1|25|SECURITY_CALL_COMPLAINT|5005|7|3');
el('securityAgencyComplaint').value='5005';
el('securityAgencyComplaint').listeners.change();
assert.equal(el('securityAgencyRefer').disabled,false);
el('securityAgencyRefer').listeners.click();
assert.deepEqual(calls.pop(),{
 action:'colonySecurityAgencyRefer',args:{complaint:'5005'}
});
frame('COLONY|1|25|SECURITY_AGENCY_REFERRAL|7000|5005|7|3|101|102');
assert.match(el('securityAgencyReferrals').children[0].textContent,
 /NOT a criminal conviction/);
frame('COLONY|1|25|SECURITY_NOTIFICATIONS_RESET');
frame(good);
assert.deepEqual(calls.pop(),{
 action:'colonySecurityNotifyAck',args:{sequence:'9001'}
});
assert.match(el('securityNotificationList').children[0].textContent,
 /Incoming authenticated hail/);
frame(good);
assert.equal(calls.length,0,'one ACK command per connection/sequence');
frame('COLONY|1|26|SECURITY_RESULT|SECURITY_NOTIFICATION_ACK|ACCEPTED|9001|0');
frame('COLONY|1|26|SECURITY_NOTIFICATIONS_RESET');
assert.equal(el('securityNotificationList').children.length,1);
assert.match(el('securityNotificationList').children[0].textContent,
 /No unacknowledged/);
desk.reset();
frame(good);
assert.equal(calls.pop().action,'colonySecurityNotifyAck',
 'reconnect/reset allows at-least-once retransmission');
// There is still no shortcut from a reported caller ID to arrest or force.
for(const action of calls.map(v=>v.action))
  assert.doesNotMatch(action,/arrest|police|fire|seize/i);
delete global.document;delete global.window;delete global.Option;
console.log('PASS STATUS-07I/07J: real UI parses durable owner notices, ACKs once, '+
  'replays on reconnect and requires original independent agency referrals');
