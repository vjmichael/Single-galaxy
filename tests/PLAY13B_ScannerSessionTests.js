const test=require('node:test');
const assert=require('node:assert/strict');
const {create,modes}=require('../viewer/scanner-session.js');

test('host-observed signature lock and range-driven request',()=>{
 const s=create();
 assert.deepEqual(modes,['environmental','celestial','anomaly']);
 assert.throws(()=>s.lock(17),/undiscovered/);
 assert.throws(()=>s.refinementRequest(),/Lock/);
 s.acceptContacts([{id:17,coarseX:10000,coarseY:0,
   uncertaintyMm:5000,secretPosition:9827,kind:'hidden'}]);
 assert.deepEqual(s.state().contacts,[{id:17,coarseX:10000,
   coarseY:0,uncertaintyMm:5000}]);
 s.setRadius(8000);s.lock(17);
 assert.deepEqual(s.refinementRequest(),{action:'refineSignature',
   mode:'anomaly',signature:17,radiusMm:8000});
 s.acceptContacts([]);assert.equal(s.state().selected,null);
});
test('mode switching, ownership boundary and malformed reports',()=>{
 const s=create();
 assert.throws(()=>s.setRadius(-1),/radius/);
 assert.throws(()=>s.selectMode('supernatural'),/mode/);
 assert.throws(()=>s.acceptContacts([{id:0,coarseX:0,coarseY:0,uncertaintyMm:5}]),/signature/);
 assert.throws(()=>s.acceptContacts([{id:5,coarseX:0,coarseY:0,uncertaintyMm:5},
   {id:5,coarseX:0,coarseY:0,uncertaintyMm:5}]),/signature/);
 s.acceptContacts([{id:4,coarseX:0,coarseY:0,uncertaintyMm:5000}]);
 s.lock(4);s.selectMode('celestial');
 assert.equal(s.state().selected,null);
 assert.deepEqual(s.state().contacts,[]);
});

test('strict native coarse signatures and durable focused probe telemetry',()=>{
 const {decode}=require('../viewer/colony.js');
 assert.deepEqual(decode('COLONY|1|12|SIGNATURE_RESET'),
   {tick:12,type:'SIGNATURE_RESET'});
 assert.deepEqual(decode('COLONY|1|12|SIGNATURE_DONE|1'),
   {tick:12,type:'SIGNATURE_DONE',count:1});
 assert.deepEqual(decode('COLONY|1|12|SIGNATURE_CONTACT|8|16000000|0|25000'),
   {tick:12,type:'SIGNATURE_CONTACT',id:8,x:16000000,y:0,uncertainty:25000});
 const row=decode('COLONY|1|12|SURVEY_PROBE|2|1|5000000|0|0|0|0|0|8|16000000|0');
 assert.equal(row.focus,8);
 assert.equal(row.fx,16000000);
 assert.equal(row.phase,0);
 for(const forged of ['COLONY|1|12|SIGNATURE_CONTACT|8|-0|0|25000',
   'COLONY|1|12|SIGNATURE_CONTACT|8|1|0|0',
   'COLONY|1|12|SURVEY_PROBE|2|1|0|0|1|8|0|0|8|16000000|0'])
   assert.throws(()=>decode(forged));
});
test('native UI, relay and authoritative command hooks remain connected',()=>{
 const fs=require('node:fs'),path=require('node:path');
 const read=p=>fs.readFileSync(path.resolve(__dirname,'..',p),'utf8');
 const ui=read('viewer/interactive.js'),relay=read('live/interactive_relay.py'),
   host=read('src/colony_live_host.cpp'),native=read('src/Simulation.cpp');
 for(const term of ["command('colonySignatureScan'","command('colonyProbeFocus'",
    "command('colonyProbeCancel'"])assert.ok(ui.includes(term));
 for(const term of ['colonySignatureScan','colonyProbeFocus','colonyProbeCancel'])
   assert.ok(relay.includes("action=='"+term+"'"));
 for(const term of ['PROBE_FOCUS','PROBE_CANCEL','SIGNATURE_CONTACT'])
   assert.ok(host.includes(term));
 for(const term of ['Action::FocusSurveyProbe','Action::CancelSurveyFocus',
   'pending_probe_discoveries_','focus_waypoint'])
   assert.ok(native.includes(term));
});
