'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {decode}=require('../viewer/colony.js');
const msg=s=>`COLONY|1|400|${s}`;
test('B7.0h bounded durable maintenance telemetry contracts',()=>{
  const r=decode(msg('MAINTENANCE|7|4|2|1|3|5'));
  assert.deepEqual([r.id,r.spares,r.technicians,r.enabled,r.delegatedRepairs,r.blocked],[7,4,2,1,3,5]);
  for(const invalid of [
    'MAINTENANCE|7|1001|2|1|3|5','MAINTENANCE|7|4|9|1|3|5',
    'MAINTENANCE|7|4|2|2|3|5','MAINTENANCE|7|4|2|1|3|401',
    'MAINTENANCE|0|4|2|1|3|5','MAINTENANCE|7|4|2|1|3',
    'MAINTENANCE|7|4|2|1|3|5|extra','MAINTENANCE|7|4|2|1|3|-1',
    'MAINTENANCE|7|4|2|1|3|5<script>'
  ])assert.throws(()=>decode(msg(invalid)),invalid);
});
test('B7.0h nonautonomous viability is explicit and typed',()=>{
  const a=decode(msg('MAINTENANCE_ASSESS|3|2|0|1'));
  assert.deepEqual([a.spares,a.technicians,a.blockedSites,a.ready],[3,2,0,1]);
  for(const bad of ['MAINTENANCE_ASSESS|3|2|0|2','MAINTENANCE_ASSESS|3|2|0',
    'MAINTENANCE_ASSESS|-3|2|0|1','MAINTENANCE_ASSESS|3|2|0|1|false'])
    assert.throws(()=>decode(msg(bad)));
});
test('B7.0h local UI controls and relay wire actions are bound',()=>{
  const html=fs.readFileSync(path.join(__dirname,'../viewer/interactive.html'),'utf8');
  const js=fs.readFileSync(path.join(__dirname,'../viewer/colony.js'),'utf8');
  const relay=fs.readFileSync(path.join(__dirname,'../live/interactive_relay.py'),'utf8');
  const host=fs.readFileSync(path.join(__dirname,'../src/colony_live_host.cpp'),'utf8');
  for(const id of ['colonyUtilitySpareAmount','colonyUtilitySpares','colonyUtilityTechnicianCount',
    'colonyUtilityStaff','colonyUtilityAutoOn','colonyUtilityAutoOff','maintenanceViability'])
      assert.ok(html.includes(`id="${id}"`),id);
  for(const action of ['colonyUtilitySpares','colonyUtilityStaff','colonyUtilityAuto']){
    assert.ok(js.includes(action),action);assert.ok(relay.includes(action),action);
  }
  for(const protocol of ['UTILITY_SPARES','UTILITY_STAFF','UTILITY_AUTO','MAINTENANCE_ASSESS'])
    assert.ok(host.includes(protocol),protocol);
});
