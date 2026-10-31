'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {decode}=require('../viewer/colony.js');
function read(t,...parts){return decode(`COLONY|1|20|${t}|${parts.join('|')}`);}
test('player outpost/farm telemetry decodes with bounded inventory and headcount',()=>{
  assert.deepEqual(read('OUTPOST',7,1).farm,1);
  const farm=read('FARM',7,8,210,300,180,120,75,24,150,80,3,0,9);
  assert.equal(farm.id,7);assert.equal(farm.workers,8);assert.equal(farm.recent,80);
  assert.equal(farm.production,9);
});
test('three-cycle production assessment is not a promise of full self-sufficiency',()=>{
  const a=read('ASSESS',24,8,96,72,1);
  assert.equal(a.totalCrew,24);assert.equal(a.foodSustained,1);
  assert.equal(a.demandFood,72);
});
test('reject malformed, excessive or fabricated farm/viability telemetry',()=>{
  for(const r of [
    'COLONY|1|20|OUTPOST|7|2',
    'COLONY|1|20|OUTPOST|0|1',
    'COLONY|1|20|FARM|7|9|210|300|180|120|75|24|150|80|3|0|9',
    'COLONY|1|20|FARM|7|8|210|300|180|120|75|24|150|80|3|4|9',
    'COLONY|1|20|ASSESS|24|25|96|72|1',
    'COLONY|1|20|ASSESS|24|8|96|72|2',
    'COLONY|1|20|ASSESS|24|8|96|72|1|garbage'
  ])assert.throws(()=>decode(r));
});
test('controls available for building, staffing, local stock transfer',()=>{
 const html=fs.readFileSync(path.join(__dirname,'../viewer/interactive.html'),'utf8');
 const js=fs.readFileSync(path.join(__dirname,'../viewer/colony.js'),'utf8');
 for(const id of ['colonyFarmOutpost','colonyFarmBuild','colonyFarmAssign','colonyFarmRecall',
                  'colonyFarmResource','colonyFarmAmount','colonyFarmSupply','farmTelemetry']){
   assert.match(html,new RegExp(`id="${id}"`));assert.match(js,new RegExp(`\\b${id}\\b`));
 }
});
