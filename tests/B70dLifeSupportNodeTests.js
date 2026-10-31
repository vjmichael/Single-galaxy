'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const contract=require('../viewer/colony.js');
const life=(stage='STABLE')=>`COLONY|1|40|LIFE|975|2990|1000|900|1100|60|20|24|2|0|40|${stage}|90`;
test('B7.0d accepts bounded life-support telemetry',()=>{
  const decoded=contract.decode(life());
  assert.equal(decoded.type,'LIFE');
  assert.equal(decoded.stage,'STABLE');
  assert.equal(decoded.food,975);
  assert.equal(decoded.reserves,40);
});
test('B7.0d rejects forged stage, truncated payload and malformed numbers',()=>{
  assert.throws(()=>contract.decode(life('ADMIN_APPROVED')));
  assert.throws(()=>contract.decode(life().replace('|40|STABLE','|+40|STABLE')));
  assert.throws(()=>contract.decode(life().replace('|STABLE|90','|STABLE')));
  assert.throws(()=>contract.decode(life().replace('|975|','|-975|')));
});
test('B7.0d has actual agriculture controls without player authority arguments',()=>{
  const viewer=fs.readFileSync(path.join(__dirname,'../viewer/interactive.html'),'utf8');
  const ui=fs.readFileSync(path.join(__dirname,'../viewer/colony.js'),'utf8');
  for(const id of ['lifeFood','lifeWater','lifeOxygen','lifePlant','lifeHarvest','lifeRecycleWater','lifeRecycleCompost','lifeOxygenAction','lifeLoadWater']){
    assert.match(viewer,new RegExp(`id="${id}"`));assert.match(ui,new RegExp(id));
  }
  assert.doesNotMatch(viewer,/onclick=/i);
});
