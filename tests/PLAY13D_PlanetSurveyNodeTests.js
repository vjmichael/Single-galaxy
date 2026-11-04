const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {decode}=require('../viewer/colony.js');
const root=path.resolve(__dirname,'..');
test('planet region decodes verified authoritative geography',()=>{
  const value=decode('COLONY|1|5|PLANET_REGION|4294967300|17|36|-120|0|7|2|1800|18|36|1');
  assert.equal(value.id,4294967300);
  assert.equal(value.elevation,-120);
  assert.equal(value.watercourse,2);
  assert.equal(value.revision,1);
});
test('reject malformed and tampered planetary survey frames',()=>{
  for(const raw of [
    'COLONY|1|5|PLANET_REGION|4294967300|17|36|-120|0|7|2|1800|18|36',
    'COLONY|1|5|PLANET_REGION|4294967300|36|36|-120|0|7|2|1800|18|36|1',
    'COLONY|1|5|PLANET_REGION|4294967300|17|36|-120|0|7|4|1800|18|36|1',
    'COLONY|1|5|PLANET_REGION|4294967300|17|36|-120|0|7|2|1800|18|-2|1',
    'COLONY|1|5|PLANET_REGION|4294967300|17|36|-120|0|7|2|1800|18|36|0'
  ])assert.throws(()=>decode(raw),Error,raw);
});
test('relay and viewer route through canonical geographic survey action',()=>{
  const relay=fs.readFileSync(path.join(root,'live/interactive_relay.py'),'utf8');
  const ui=fs.readFileSync(path.join(root,'viewer/colony.js'),'utf8');
  assert.match(relay,/action=='colonyPlanetSurvey'/);
  assert.match(relay,/command=f'PLANET_SURVEY/);
  assert.match(ui,/GalaxyColonyCommand\?\.\('colonyPlanetSurvey'/);
});

test('regional facility receipts and source waypoints require canonical native telemetry',()=>{
  const receipt=decode('COLONY|1|9|PLANET_OPERATION_COMMITTED|4294967300|17|36|PLANET_BUILD|22');
  assert.equal(receipt.action,'PLANET_BUILD');
  assert.equal(receipt.generation,22);
  const source=decode('COLONY|1|9|OASIS_REGION|4294967300|17|36|1');
  assert.equal(source.latitude,17);
  assert.equal(source.longitude,36);
  for(const invalid of [
    'COLONY|1|9|PLANET_OPERATION_COMMITTED|4294967300|17|36|PLANET_FREE_MONEY|22',
    'COLONY|1|9|PLANET_OPERATION_COMMITTED|4294967300|17|36|PLANET_BUILD|0',
    'COLONY|1|9|PLANET_OPERATION_COMMITTED|4294967300|36|36|PLANET_BUILD|22',
    'COLONY|1|9|OASIS_REGION|4294967300|17|72|1',
    'COLONY|1|9|OASIS_REGION|4294967300|17|36|0'
  ])assert.throws(()=>decode(invalid),Error,invalid);
});
test('regional construction operations use authenticated command and native receipts',()=>{
  const relay=fs.readFileSync(path.join(root,'live/interactive_relay.py'),'utf8');
  const ui=fs.readFileSync(path.join(root,'viewer/colony.js'),'utf8');
  const host=fs.readFileSync(path.join(root,'src/colony_live_host.cpp'),'utf8');
  for(const action of ['colonyPlanetBuild','colonyPlanetEquip','colonyPlanetSupply',
      'colonyPlanetGrow','colonyPlanetExtract','colonyPlanetRetrieve']){
    assert.match(ui,new RegExp(action));
    assert.match(relay,new RegExp(action));
  }
  assert.match(host,/known_planetary_region/);
  assert.match(host,/PLANET_OPERATION_COMMITTED/);
  assert.match(ui,/OASIS_REGION/);
});
