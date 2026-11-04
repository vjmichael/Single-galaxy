const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const {decode}=require('../viewer/colony.js');
test('strict native geological drone status including sample and flight phase',()=>{
 const row=decode('COLONY|1|15|SURVEY_PROBE|900000000003|1|15000|0|2|8|1|3');
 assert.deepEqual(row,{tick:15,type:'SURVEY_PROBE',id:900000000003,system:1,
   x:15000,y:0,phase:2,target:8,sample:1,taxonomy:3});
 assert.equal(decode('COLONY|1|15|SURVEY_PROBE|3|1|0|0|3|8|1|3').phase,3);
 for(const bad of ['COLONY|1|15|SURVEY_PROBE|3|1|0|0|4|8|0|0',
    'COLONY|1|15|SURVEY_PROBE|3|1|0|0|1|0|0|0',
    'COLONY|1|15|SURVEY_PROBE|3|1|0|0|0|8|0|0',
    'COLONY|1|15|SURVEY_PROBE|3|1|0|0|2|8|1|0',
    'COLONY|1|15|SURVEY_PROBE|3|1|0|0|2|8|2|3'])
    assert.throws(()=>decode(bad),bad);
});
test('real native target, owner-authorized drone launch and physical recovery controls',()=>{
 const host=read('src/colony_live_host.cpp'),relay=read('live/interactive_relay.py');
 const html=read('viewer/interactive.html'),ui=read('viewer/interactive.js');
 const sim=read('src/Simulation.cpp');
 assert.ok(html.includes('id="droneLaunch"'));
 assert.ok(html.includes('id="droneTarget"'));
 assert.ok(ui.includes("command('colonyDroneLaunch',{target})"));
 assert.ok(relay.includes("action=='colonyDroneLaunch'"));
 assert.ok(relay.includes("command=f'DRONE_LAUNCH|{target}'"));
 assert.ok(host.includes('c.action=Action::LaunchGeologicalDrone'));
 assert.ok(sim.includes('case Action::LaunchGeologicalDrone:'));
 assert.ok(sim.includes('--it->second.ore'));
 assert.ok(sim.includes('p.sample_ore=1'));
 assert.ok(sim.includes('actor.cargo+=sample'));
 assert.ok(!ui.includes('p.sample_ore='));
});
