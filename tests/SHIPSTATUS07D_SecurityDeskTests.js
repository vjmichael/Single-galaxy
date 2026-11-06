'use strict';
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const security=require('../viewer/security-desk.js');
const key='4123456789012345678'; // beyond 53-bit JS integer precision
const hex=Buffer.from('COLONY_HULL|HULL_100|CARGO_100').toString('hex');
assert.equal(security.caseKey(key),key);
assert.equal(security.decodeHex(hex),'COLONY_HULL|HULL_100|CARGO_100');
assert.equal(security.parse(
  'COLONY|1|15|SECURITY_STATUS|8|8|1|1|3').claimed,true);
assert.equal(security.parse(
  'COLONY|1|15|SECURITY_CASE|'+key+'|12|1|2|'+hex+'|0|0').id,key);
for(const forged of [
 'COLONY|1|15|SECURITY_CASES_RESET|injected',
 'COLONY|1|15|SECURITY_CASE|0001|12|1|2|'+hex+'|0|0',
 'COLONY|1|15|SECURITY_CASE|4611686018427387904|12|1|2|'+hex+'|0|0',
 'COLONY|1|15|SECURITY_CASE|'+key+'|12|1|2|3c7363726970743e|0|0',
 'COLONY|1|15|SECURITY_CASE|'+key+'|12|1|2|'+hex+'|7|0',
 'COLONY|1|15|SECURITY_CASE|'+key+'|12|1|2|'+hex+'|0|999',
 'COLONY|1|15|SECURITY_CONTACT|2|4|'+hex,
 'COLONY|1|15|SECURITY_NOTICE|2|0|9',
 'COLONY|1|15|SECURITY_STATUS|8|8|3|1|3',
 'COLONY|1|15|SECURITY_RESULT|SECURITY_HAIL|ACCEPTED|0|1',
 'COLONY|1|15|SECURITY_RESULT|SECURITY_ARREST|ACCEPTED|55|0'
])assert.throws(()=>security.parse(forged),undefined,forged);
console.log('PASS SHIP-STATUS-07D: native security parser, 62-bit case identity and anti-injection');

class FakeOption{
 constructor(text,value){this.text=text;this.value=value;}
}
class FakeNode{
 constructor(){this.children=[];this.options=[];this.listeners={};
   this.value='';this.textContent='';this.disabled=false;this.style={};}
 replaceChildren(...children){
   this.children=children;
   this.options=children.filter(c=>c instanceof FakeOption);
   if(this.options.length&&!this.options.some(x=>x.value===this.value))
     this.value=this.options[0].value;
 }
 append(...children){this.children.push(...children);}
 addEventListener(name,callback){this.listeners[name]=callback;}
}
const nodes=new Map(),requests=[];
global.Option=FakeOption;
global.document={
 getElementById(id){
   if(!nodes.has(id))nodes.set(id,new FakeNode());
   return nodes.get(id);
 },
 createElement(){return new FakeNode();}
};
global.window={GalaxyColonyCommand:(action,args)=>{
 requests.push([action,args]);return Promise.resolve(true);
}};
delete require.cache[require.resolve('../viewer/security-desk.js')];
const desk=require('../viewer/security-desk.js');
const e=id=>global.document.getElementById(id);
const frame=value=>assert.equal(desk.accept(value),true);
assert.equal(e('securityClaim').disabled,true);
frame('COLONY|1|15|SECURITY_STATUS|8|8|0|1|1');
assert.equal(e('securityClaim').disabled,false);
e('securityClaimRadius').value='3000';
e('securityClaim').listeners.click();
assert.deepEqual(requests.pop(),['colonySecurityClaim',{radius:3000}]);
assert.match(e('securityDeskFeedback').textContent,/awaiting committed receipt/);
frame('COLONY|1|15|SECURITY_RESULT|SECURITY_CLAIM|ACCEPTED|900000000001|0');
assert.match(e('securityDeskFeedback').textContent,/native ACCEPTED/);
frame('COLONY|1|15|SECURITY_STATUS|8|8|1|1|3');
frame('COLONY|1|15|SECURITY_CONTACTS_RESET');
frame('COLONY|1|15|SECURITY_CONTACT|22|3|'+hex);
frame('COLONY|1|15|SECURITY_CASES_RESET');
frame('COLONY|1|15|SECURITY_CASE|'+key+'|12|1|2|'+hex+'|0|0');
const view=e('securityCaseList').children[0].textContent;
assert.match(view,/culprit unidentified/);
assert.doesNotMatch(view,/LATER authenticated controller/);
e('securityContact').value='22';
e('securityCaseSelect').value=key;
frame('COLONY|1|15|SECURITY_REFRESH_DONE');
assert.equal(e('securityWarn').disabled,false);
assert.equal(e('securityPatrol').disabled,false);
e('securityWarn').listeners.click();
assert.deepEqual(requests.pop(),['colonySecurityWarn',{ship:22}]);
e('securityPatrol').listeners.click();
assert.deepEqual(requests.pop(),['colonySecurityPatrol',{case:key,ship:22}]);
// No extra client-supplied owner, juror, NPC, warrant, or alleged pilot.
frame('COLONY|1|15|SECURITY_DESTINATION|123');
e('securityDestination').value='123';
e('securityDestination').listeners.change();
assert.equal(e('securityHail').disabled,false);
e('securityHail').listeners.click();
assert.deepEqual(requests.pop(),['colonySecurityHail',{colony:123}]);
frame('COLONY|1|15|SECURITY_NOTICE|123|8|900000000003');
e('securityNotice').value='123:900000000003';
e('securityNotice').listeners.change();
assert.equal(e('securityAcknowledge').disabled,false);
e('securityAcknowledge').listeners.click();
assert.deepEqual(requests.pop(),['colonySecurityReply',{
  colony:123,notice:900000000003,choice:'acknowledge'}]);
frame('COLONY|1|15|SECURITY_NOTICE|124|9|900000000004');
e('securityNotice').value='124:900000000004';
e('securityNotice').listeners.change();
assert.equal(e('securityRefuse').disabled,true,
 'cannot answer a notice addressed to another physical ship');
frame('COLONY|1|15|SECURITY_CASES_RESET');
frame('COLONY|1|15|SECURITY_CASE|'+key+'|12|1|2|'+hex+'|7|900000000005');
assert.match(e('securityCaseList').children[0].textContent,/LATER authenticated controller #7/);
assert.match(e('securityCaseList').children[0].textContent,/NOT proof of the earlier pilot/);
frame('COLONY|1|15|SECURITY_RESULT|SECURITY_HAIL|ACCEPTED|900000000005|1');
assert.match(e('securityDeskFeedback').textContent,/linked/);
desk.reset();
assert.equal(e('securityCaseList').children.length,1);
assert.match(e('securityCaseList').children[0].textContent,/No witnessed cyber incidents/);
assert.equal(e('securityPatrol').disabled,true);
const html=fs.readFileSync(path.join(__dirname,'../viewer/interactive.html'),'utf8');
for(const id of ['securityDeskStatus','securityCaseList','securityHail',
 'securityNotice','securityWarn','securityPatrol','securityAcknowledge',
 'securityRefuse','securityDeskFeedback'])
 assert.match(html,new RegExp('id="'+id+'"'));
delete global.Option;delete global.document;delete global.window;
console.log('PASS SHIP-STATUS-07D: real DOM security desk, voluntary hail, private logout, no fabricated verdict');
