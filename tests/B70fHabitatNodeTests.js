'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {decode}=require('../viewer/colony.js');
const msg=(tag)=>`COLONY|1|80|${tag}`;
test('B7.0f habitat telemetry is strictly bounded and typed',()=>{
  const h=decode(msg('HABITAT|7|3|8|7|240|300|240|3|3|0'));
  assert.equal(h.type,'HABITAT');
  assert.equal(h.residents,3);assert.equal(h.occupied,7);assert.equal(h.comfort,3);
  for(const invalid of [
    'HABITAT|7|3|8|9|240|300|240|3|3|0',
    'HABITAT|7|7|8|3|240|300|240|3|3|0',
    'HABITAT|7|3|8|7|240|300|240|3|4|0',
    'HABITAT|7|3|8|7|240|300|240|3|3|4',
    'HABITAT|7|3|8|7|240|300|240|3|3|-1',
    'HABITAT|7|3|8|7|240|300|240|3|3|0|EXTRA',
    'HABITAT|7|3|8|7|240|300|240|3|3|0|<script>'
  ])assert.throws(()=>decode(msg(invalid)));
});
test('B7.0f assessment distinguishes housing from full autonomy',()=>{
  const a=decode(msg('HAB_ASSESS|3|7|8|0|1|LIVABLE'));
  assert.equal(a.stage,'LIVABLE');assert.equal(a.housed,7);
  for(const bad of [
    'HAB_ASSESS|3|9|8|0|1|LIVABLE',
    'HAB_ASSESS|3|7|8|0|2|LIVABLE',
    'HAB_ASSESS|3|7|8|0|1|MAGIC',
    'HAB_ASSESS|3|7|8|0|1|LIVABLE|EXTRA'
  ])assert.throws(()=>decode(msg(bad)));
});
test('B7.0f housing controls provide a real local command path',()=>{
  const js=fs.readFileSync(path.join(__dirname,'../viewer/colony.js'),'utf8');
  const html=fs.readFileSync(path.join(__dirname,'../viewer/interactive.html'),'utf8');
  for(const action of ['colonyHabBuild','colonyHabResidents','colonyHabStock'])
    assert.ok(js.includes(action));
  for(const id of ['colonyHabOutpost','colonyHabBuild','colonyHabAssign',
       'colonyHabRecall','colonyHabSupply','habViability','habTelemetry'])
    assert.ok(html.includes(`id="${id}"`));
});
