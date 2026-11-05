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

test('orbit-only NPC roster and spaceport construction status decode',()=>{
  const crew=decode('COLONY|1|13|PLANET_CREW_AVAILABLE|42');
  assert.equal(crew.id,42);
  assert.equal(decode('COLONY|1|13|PLANET_CREW_DONE').type,'PLANET_CREW_DONE');
  const status=decode('COLONY|1|13|PLANET_SITE_STATUS|4294967300|17|36|2|19|240|2|35|28|26|42|43|0|100|0|0|0|0');
  assert.equal(status.phase,2);
  assert.equal(status.progress,19);
  assert.equal(status.required,240);
  assert.equal(status.npc1,42);
  assert.equal(status.npc2,43);
  for(const bad of [
    'COLONY|1|13|PLANET_SITE_STATUS|4294967300|17|36|4|19|240|2|35|28|26|42|43|0|100|0|0|0|0',
    'COLONY|1|13|PLANET_SITE_STATUS|4294967300|17|36|3|19|240|2|35|28|26|42|43|0|100|0|0|0|0',
    'COLONY|1|13|PLANET_SITE_STATUS|4294967300|17|36|2|19|240|2|35|28|26|42|42|0|100|0|0|0|0',
    'COLONY|1|13|PLANET_SITE_STATUS|4294967300|17|36|2|19|240|2|35|28|26|42',
    'COLONY|1|13|PLANET_CREW_AVAILABLE|0'
  ]) assert.throws(()=>decode(bad),Error,bad);
});
test('surface construction controls remain native and have no surface viewer',()=>{
  const ui=fs.readFileSync(path.join(root,'viewer/colony.js'),'utf8');
  const relay=fs.readFileSync(path.join(root,'live/interactive_relay.py'),'utf8');
  assert.match(ui,/colonyPlanetCrew/);
  assert.match(ui,/colonyPlanetStatus/);
  assert.match(ui,/colonyPlanetSupport/);
  assert.match(relay,/firstNpc/);
  assert.match(relay,/secondNpc/);
  assert.match(relay,/PLANET_SITE_STATUS/);
  assert.match(relay,/PLANET_SUPPORT/);
});

test('owner-scoped planetary NPC policies and work telemetry are validated',()=>{
  const receipt=decode('COLONY|1|10|PLANET_POLICY_COMMITTED|4294967300|17|36|2|44');
  assert.equal(receipt.policy,2);
  const report=decode('COLONY|1|12|PLANET_SITE_STATUS|4294967300|17|36|3|240|240|2|77|85|67|42|43|2|99|5|1|5000|0');
  assert.equal(report.policy,2);
  assert.equal(report.cycles,5);
  assert.equal(report.extracted,5000);
  assert.equal(report.condition,99);
  for(const raw of [
    'COLONY|1|10|PLANET_POLICY_COMMITTED|4294967300|17|36|5|44',
    'COLONY|1|10|PLANET_POLICY_COMMITTED|4294967300|17|36|2|0',
    'COLONY|1|12|PLANET_SITE_STATUS|4294967300|17|36|3|240|240|2|77|85|67|42|43|5|99|5|1|5000|0',
    'COLONY|1|12|PLANET_SITE_STATUS|4294967300|17|36|3|240|240|2|77|85|67|42|43|2|101|5|1|5000|0',
    'COLONY|1|12|PLANET_SITE_STATUS|4294967300|17|36|3|240|240|2|77|85|67|42|43|2|99|5|1|5000'
  ])assert.throws(()=>decode(raw),Error,raw);
  const relay=fs.readFileSync(path.join(root,'live/interactive_relay.py'),'utf8');
  assert.match(relay,/action=='colonyPlanetPolicy'/);
  assert.match(relay,/PLANET_POLICY/);
});
