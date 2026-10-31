'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {decode}=require('../viewer/colony.js');
const packet=s=>`COLONY|1|200|${s}`;
test('B7.0k clinic packet validates real inputs, patient outcomes and continuity',()=>{
  const p=decode(packet('CLINIC|7|2|19|19|0|9|3|2|1|1'));
  assert.deepEqual([p.id,p.medics,p.water,p.substrate,p.active,p.cycles,p.care,p.shortages,p.treated,p.recorded],
    [7,2,19,19,0,9,3,2,1,1]);
  for(const bad of [
    'CLINIC|0|2|19|19|0|9|3|2|1|1','CLINIC|7|5|19|19|0|9|3|2|1|1',
    'CLINIC|7|2|19|19|0|9|10|2|1|1','CLINIC|7|2|19|19|2|9|3|2|1|1',
    'CLINIC|7|2|19|19|0|9|3|2|2|1','CLINIC|7|2|19|19|0|9|3|2|1|2',
    'CLINIC|7|2|19|19|0|9|3|2|1','CLINIC|7|2|19|19|0|9|3|2|1|1|EXTRA',
    'CLINIC|7|2|19|19|-1|9|3|2|1|1'
  ])assert.throws(()=>decode(packet(bad)),bad);
});
test('B7.0k clinic readiness never advertises growth or full healthcare',()=>{
  const yes=decode(packet('CLINIC_ASSESS|2|2|0|5|1'));
  assert.equal(yes.access,1);
  const no=decode(packet('CLINIC_ASSESS|1|2|1|5|0'));
  assert.equal(no.access,0);
  for(const bad of ['CLINIC_ASSESS|1|2|0|5|1',
     'CLINIC_ASSESS|2|2|1|5|1','CLINIC_ASSESS|2|1|0|5|0',
     'CLINIC_ASSESS|0|0|0|0|1','CLINIC_ASSESS|0|2|0|5|2'])
     assert.throws(()=>decode(packet(bad)),bad);
  const previous=decode(packet('RESILIENCE|20|10|1|1|1|1|3|1|1|1|0|3840'));
  assert.equal(previous.growth,0);assert.ok(previous.blockers&512);
});
test('B7.0k UI, relay and host expose only bounded clinic commands',()=>{
  const root=path.join(__dirname,'..');
  const html=fs.readFileSync(path.join(root,'viewer/interactive.html'),'utf8');
  const js=fs.readFileSync(path.join(root,'viewer/colony.js'),'utf8');
  const relay=fs.readFileSync(path.join(root,'live/interactive_relay.py'),'utf8');
  const host=fs.readFileSync(path.join(root,'src/colony_live_host.cpp'),'utf8');
  for(const id of ['colonyClinicOutpost','colonyClinicBuild','colonyClinicStaffCount',
       'colonyClinicStaff','colonyClinicResource','colonyClinicAmount',
       'colonyClinicSupply','clinicTelemetry','clinicReadiness'])
    assert.ok(html.includes(`id="${id}"`),id);
  for(const protocol of ['CLINIC_BUILD','CLINIC_STOCK','CLINIC_STAFF','CLINIC_ASSESS','CLINIC|'])
    assert.ok(host.includes(protocol),protocol);
  for(const action of ['colonyClinicBuild','colonyClinicStaff','colonyClinicStock']){
    assert.ok(relay.includes(action),action);assert.ok(js.includes(action),action);
  }
  assert.ok(!relay.includes('colonyClinicGrantAutonomousGrowth'));
});
