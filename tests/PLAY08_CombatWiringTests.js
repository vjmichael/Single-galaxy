const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..');
const read=p=>fs.readFileSync(path.join(root,p),'utf8');
const colony=require('../viewer/colony.js');
const html=read('viewer/interactive.html');
const relay=read('live/interactive_relay.py');
const interactive=read('viewer/interactive.js');
test('native combat telemetry validates bounded hull, ammunition, target identity',()=>{
 const decode=colony.decode;
 assert.deepEqual(decode('COLONY|1|3|COMBAT_SHIP|100|12'),
  {tick:3,type:'COMBAT_SHIP',hull:100,charges:12});
 assert.deepEqual(decode('COLONY|1|3|COMBAT_TARGET|7|45'),
  {tick:3,type:'COMBAT_TARGET',id:7,hull:45});
 for(const row of ['COLONY|1|3|COMBAT_SHIP|101|12','COLONY|1|3|COMBAT_SHIP|100|13',
  'COLONY|1|3|COMBAT_TARGET|0|10','COLONY|1|3|COMBAT_TARGET|-5|10',
  'COLONY|1|3|COMBAT_SHIP|x|1']){
  assert.throws(()=>decode(row),row);
 }
});
test('combat UI and authenticated relay command are connected to native training action',()=>{
 for(const id of ['combatTarget','combatFire','combatHull','combatCharges','combatFeedback'])
  assert.ok(html.includes('id="'+id+'"'),id+' missing');
 assert.ok(interactive.includes("command('colonyFireTraining',{target})"));
 assert.ok(relay.includes("'colonyFireTraining'"));
 assert.ok(relay.includes('FIRE_TRAINING|{target}'));
 assert.ok(read('src/colony_live_host.cpp').includes('Action::FireTrainingWeapon'));
 assert.ok(read('src/Simulation.cpp').includes('case Action::FireTrainingWeapon:'));
});
test('no browser-side combat damage, crew harm or client-authorized wreck creation',()=>{
 for(const token of ['.hull-=','weapon_charges-=','.type=EntityType::Wreck'])
  assert.ok(!interactive.includes(token));
 assert.ok(read('src/Simulation.cpp').includes('!it->second.training_drone'));
 assert.ok(read('src/Simulation.cpp').includes('colony_ships_.count(c.target)'));
});
