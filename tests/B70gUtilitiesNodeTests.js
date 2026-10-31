'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {decode}=require('../viewer/colony.js');
const msg=x=>`COLONY|1|88|${x}`;
test('B7.0g typed utility status preserves service, wear and repair readings',()=>{
  const u=decode(msg('UTILITY|7|70|95|1|4|4|0|0'));
  assert.deepEqual([u.id,u.condition,u.stored,u.powered,u.cycles,u.reliable,u.repairs],[7,70,95,1,4,4,0]);
  for(const invalid of [
    'UTILITY|7|101|95|1|4|4|0|0',
    'UTILITY|7|70|121|1|4|4|0|0',
    'UTILITY|7|70|95|2|4|4|0|0',
    'UTILITY|7|70|95|1|4|5|0|0',
    'UTILITY|7|70|95|1|4|4|5|0',
    'UTILITY|0|70|95|1|4|4|0|0',
    'UTILITY|7|70|95|1|4|4|0|0|EXTRA',
    'UTILITY|7|70|95|1|4|4|0|-1',
    'UTILITY|7|70|95|1|4|4|0|0|<script>'
  ])assert.throws(()=>decode(msg(invalid)),invalid);
});
test('B7.0g utility assessment never implies autonomous growth',()=>{
  const x=decode(msg('UTILITY_ASSESS|1|0|1'));
  assert.equal(x.sustained,1);
  for(const invalid of ['UTILITY_ASSESS|1|0|2','UTILITY_ASSESS|1|0|1|EXTRA',
    'UTILITY_ASSESS|-1|0|0'])assert.throws(()=>decode(msg(invalid)));
});
test('B7.0g visual controls connect to explicit local commands',()=>{
  const js=fs.readFileSync(path.join(__dirname,'../viewer/colony.js'),'utf8');
  const html=fs.readFileSync(path.join(__dirname,'../viewer/interactive.html'),'utf8');
  for(const action of ['colonyUtilityBuild','colonyUtilityRepair'])assert.ok(js.includes(action));
  for(const id of ['colonyUtilityOutpost','colonyUtilityBuild','colonyUtilityRepair','utilityViability','utilityTelemetry'])
    assert.ok(html.includes(`id="${id}"`),id);
});
