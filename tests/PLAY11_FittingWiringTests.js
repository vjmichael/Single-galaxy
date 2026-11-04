const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..');
const read=p=>fs.readFileSync(path.join(root,p),'utf8');
const {decode}=require('../viewer/colony.js');
test('native fitting telemetry validates fixed rack capacities and scanner state',()=>{
 assert.deepEqual(decode('COLONY|1|8|FITTING|1|2|2|2|1'),
 {tick:8,type:'FITTING',installed:true,hardpoints:2,support:2,subsystems:2,rigging:1});
 assert.deepEqual(decode('COLONY|1|8|FITTING|0|2|2|2|1').installed,false);
 for(const msg of ['COLONY|1|8|FITTING|2|2|2|2|1',
   'COLONY|1|8|FITTING|1|3|2|2|1','COLONY|1|8|FITTING|1|2|2|2',
   'COLONY|1|8|FITTING|1|2|2|2|9'])
  assert.throws(()=>decode(msg),msg);
});
test('always-on proximity sensor is separate from the exploration module',()=>{
 assert.deepEqual(decode('COLONY|1|9|PROXIMITY|0|0'),{
   tick:9,type:'PROXIMITY',id:0,distance:0});
 assert.deepEqual(decode('COLONY|1|9|PROXIMITY|3|1234').distance,1234);
 assert.deepEqual(decode('COLONY|1|9|PROXIMITY|3|0').distance,0);
 for(const msg of ['COLONY|1|9|PROXIMITY|0|250','COLONY|1|9|PROXIMITY|3|5001'])
  assert.throws(()=>decode(msg),msg);
});
test('native hardpoint operation passes through owner-authenticated local relay',()=>{
 const html=read('viewer/interactive.html'),client=read('viewer/interactive.js');
 const host=read('src/colony_live_host.cpp'),relay=read('live/interactive_relay.py');
 for(const id of ['fittingCapacity','fitScanner','unfitScanner','proximityStatus'])
  assert.ok(html.includes('id="'+id+'"'),id);
 assert.ok(client.includes("command('colonyConfigureScanner',{mode:1})"));
 assert.ok(client.includes("command('colonyConfigureScanner',{mode:-1})"));
 assert.ok(relay.includes("action=='colonyConfigureScanner'"));
 assert.ok(relay.includes('FIT_SCANNER|{mode}'));
 assert.ok(host.includes('c.action=Action::ConfigureExplorationScanner'));
 assert.ok(host.includes('provision_starter_exploration_scanner(initial_ship)'));
 assert.ok(read('src/Simulation.cpp').includes('if(!actor.fitting.exploration_scanner())'));
});
