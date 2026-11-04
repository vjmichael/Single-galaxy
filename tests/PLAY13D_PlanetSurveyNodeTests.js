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
