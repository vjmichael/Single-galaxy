'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {decode}=require('../viewer/colony.js');
const packet=v=>`COLONY|1|200|${v}`;
test('B7.0m audit distinguishes declared policy, staff and observed operations',()=>{
 const strict=decode(packet('POLICY_AUDIT|7|0|3|0|3|0|0|0'));
 assert.equal(strict.declared,3);assert.equal(strict.inspectors,0);
 assert.equal(strict.staffed,0);assert.equal(strict.supported,0);
 const staffed=decode(packet('POLICY_AUDIT|7|0|3|3|3|0|1|0'));
 assert.equal(staffed.staffed,1);assert.equal(staffed.supported,0);
 const supported=decode(packet('POLICY_AUDIT|7|0|3|3|3|1|1|1'));
 assert.equal(supported.supported,1);
 const noPolicy=decode(packet('POLICY_AUDIT|7|1|0|0|0|1|1|0'));
 assert.equal(noPolicy.supported,0);
});
test('B7.0m contradictory or noncanonical audit frames reject',()=>{
 for(const bad of [
  'POLICY_AUDIT|0|0|3|3|3|1|1|1',
  'POLICY_AUDIT|7|4|3|3|3|1|1|1',
  'POLICY_AUDIT|7|0|4|3|3|1|1|1',
  'POLICY_AUDIT|7|0|3|4|3|1|1|1',
  'POLICY_AUDIT|7|0|3|3|2|1|1|1',
  'POLICY_AUDIT|7|0|3|2|3|1|1|1',
  'POLICY_AUDIT|7|0|3|3|3|0|1|1',
  'POLICY_AUDIT|7|1|0|0|0|1|1|1',
  'POLICY_AUDIT|7|0|3|3|3|1|1',
  'POLICY_AUDIT|7|0|3|3|3|1|1|1|9',
  'POLICY_AUDIT|7|0|-1|3|3|1|1|1',
  'POLICY_AUDIT|7|0|03|3|3|1|1|1',
 ])assert.throws(()=>decode(packet(bad)),bad);
});
test('B7.0m dashboard, host and relay expose bounded policy commands',()=>{
 const root=path.join(__dirname,'..');
 const html=fs.readFileSync(path.join(root,'viewer/interactive.html'),'utf8');
 const js=fs.readFileSync(path.join(root,'viewer/colony.js'),'utf8');
 const relay=fs.readFileSync(path.join(root,'live/interactive_relay.py'),'utf8');
 const host=fs.readFileSync(path.join(root,'src/colony_live_host.cpp'),'utf8');
 for(const id of ['colonyPolicyOutpost','colonyPolicyDomain','colonyPolicyLevel',
   'colonyPolicyStaffCount','colonyPolicySet','colonyPolicyStaff','colonyPolicyTelemetry'])
   assert.ok(html.includes(`id="${id}"`),id);
 assert.ok(js.includes('POLICY_AUDIT')&&js.includes('colonyPolicySet'));
 assert.ok(relay.includes("type(domain)!=int")&&relay.includes('colonyPolicyStaff'));
 assert.ok(host.includes('POLICY_SET')&&host.includes('POLICY_AUDIT|'));
});
