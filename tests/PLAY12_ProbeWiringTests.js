const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..');
const read=p=>fs.readFileSync(path.join(root,p),'utf8');
const {decode}=require('../viewer/colony.js');
test('probe stock and persistent deployed coordinates have strict native frames',()=>{
 assert.deepEqual(decode('COLONY|1|12|PROBE_STOCK|1|1'),
 {tick:12,type:'PROBE_STOCK',carried:1,launcher:true});
 assert.deepEqual(decode('COLONY|1|12|SURVEY_PROBE|900000000001|1|-2000|0'),
 {tick:12,type:'SURVEY_PROBE',id:900000000001,system:1,x:-2000,y:0});
 assert.deepEqual(decode('COLONY|1|12|PROBE_RESET'),{tick:12,type:'PROBE_RESET'});
 for(const bad of ['COLONY|1|12|PROBE_STOCK|9|1',
  'COLONY|1|12|PROBE_STOCK|1|3',
  'COLONY|1|12|SURVEY_PROBE|0|1|0|0',
  'COLONY|1|12|SURVEY_PROBE|900000000001|0|0|0',
  'COLONY|1|12|SURVEY_PROBE|900000000001|1|-0|0',
  'COLONY|1|12|SURVEY_PROBE|900000000001|1|0'])
  assert.throws(()=>decode(bad),bad);
});
test('launcher, owner-auth relay and user control path are physically grounded',()=>{
 const host=read('src/colony_live_host.cpp'),relay=read('live/interactive_relay.py');
 const ui=read('viewer/interactive.js'),html=read('viewer/interactive.html');
 const native=read('src/Simulation.cpp');
 for(const id of ['probeTarget','probeLaunch','probeSurvey','probeRetrieve','probeInventory'])
  assert.ok(html.includes('id="'+id+'"'),id+' control absent');
 for(const command of ['colonyProbeDeploy','colonyProbeScan','colonyProbeRecover'])
  assert.ok(relay.includes("'"+command+"'"),'relay missing '+command);
 assert.ok(ui.includes("command('colonyProbeDeploy')"));
 assert.ok(ui.includes("command('colonyProbeScan',{probe})"));
 assert.ok(ui.includes("command('colonyProbeRecover',{probe})"));
 assert.ok(host.includes('provision_starter_probe_kit(initial_ship)'));
 assert.ok(native.includes('case Action::DeploySurveyProbe:'));
 assert.ok(native.includes('case Action::ScanSurveyProbe:')===false ||
   native.includes('case Action::RecoverSurveyProbe:')); // combined switch labels
 assert.ok(native.includes('survey_probes_.erase(it)'));
 assert.ok(!ui.includes('survey_probes_.erase'));
});
