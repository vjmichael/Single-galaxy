const test=require('node:test');
const assert=require('node:assert/strict');
const {create,modes}=require('../viewer/scanner-session.js');

test('host-observed signature lock and range-driven request',()=>{
 const s=create();
 assert.deepEqual(modes,['environmental','celestial','anomaly']);
 assert.throws(()=>s.lock(17),/undiscovered/);
 assert.throws(()=>s.refinementRequest(2),/Lock/);
 s.acceptContacts([{id:17,coarseX:10000,coarseY:0,
   uncertaintyMm:5000,secretPosition:9827,kind:'hidden'}]);
 assert.deepEqual(s.state().contacts,[{id:17,coarseX:10000,
   coarseY:0,uncertaintyMm:5000}]);
 s.setRadius(8000);s.lock(17);
 assert.throws(()=>s.refinementRequest(0),/probe/);
 assert.deepEqual(s.refinementRequest(2),{action:'colonyProbeFocus',
   probe:2,signature:17});
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

test('the live scanner exposes radius and does not fake unfinished modes',()=>{
 const fs=require('node:fs'),path=require('node:path');
 const read=p=>fs.readFileSync(path.resolve(__dirname,'..',p),'utf8');
 const html=read('viewer/interactive.html'),host=read('src/colony_live_host.cpp');
 const relay=read('live/interactive_relay.py');
 for(const id of ['signatureRange','signatureMode','signatureScan','signatureTarget','signatureFocus','signatureCancel'])
   assert.ok(html.includes('id="'+id+'"'));
 assert.ok(html.includes('<option value="celestial">Celestial catalogue</option>'));
 assert.ok(html.includes('<option value="environmental">Environment survey</option>'));
 assert.ok(relay.includes("command=f'SIGNATURES|{radius}'"));
 assert.ok(host.includes('coarse_probe_signatures(principal,ship,radius)'));
});

test('preview-only modes cannot submit unsupported anomaly refinement',()=>{
 const fs=require('node:fs'),path=require('node:path');
 const ui=fs.readFileSync(path.resolve(__dirname,'../viewer/interactive.js'),'utf8');
 assert.ok(ui.includes("if(el('signatureMode').value!=='anomaly'){"));
 assert.ok(ui.includes('Only anomaly focus is implemented in the native host'));
});

test('native celestial and environmental frames validate generated metadata',()=>{
 const {decode}=require('../viewer/colony.js');
 assert.deepEqual(decode('COLONY|1|7|CELESTIAL_HEADER|2|1|2|4'),
   {tick:7,type:'CELESTIAL_HEADER',architecture:2,morphology:1,stars:2,bands:4});
 assert.deepEqual(decode('COLONY|1|7|CELESTIAL_BAND|1|1800|42|1'),
   {tick:7,type:'CELESTIAL_BAND',ordinal:1,radius:1800,eccentricity:42,zone:1});
 assert.equal(decode('COLONY|1|7|CELESTIAL_DONE|4').count,4);
 assert.deepEqual(decode('COLONY|1|7|ENVIRONMENT_STATUS|1|0|1|250'),
   {tick:7,type:'ENVIRONMENT_STATUS',system:1,magnetized:false,dust:true,speed:250});
 for(const bad of ['COLONY|1|7|CELESTIAL_HEADER|4|1|2|4',
   'COLONY|1|7|CELESTIAL_BAND|0|1800|42|1',
   'COLONY|1|7|ENVIRONMENT_STATUS|1|2|0|5'])
   assert.throws(()=>decode(bad));
});
test('celestial and environmental modes route only to native survey endpoints',()=>{
 const fs=require('node:fs'),path=require('node:path');
 const read=p=>fs.readFileSync(path.resolve(__dirname,'..',p),'utf8');
 const ui=read('viewer/interactive.js'),relay=read('live/interactive_relay.py'),
   host=read('src/colony_live_host.cpp');
 assert.ok(ui.includes("command('colonyCelestialScan')"));
 assert.ok(ui.includes("command('colonyEnvironmentScan')"));
 assert.ok(relay.includes('SYSTEM_CELESTIAL'));
 assert.ok(relay.includes('SYSTEM_ENVIRONMENT'));
 assert.ok(host.includes('scan_celestial_profile(principal,ship)'));
 assert.ok(host.includes('scan_environment(principal,ship)'));
});

test('private celestial survey history uses strict provenance fields',()=>{
 const {decode}=require('../viewer/colony.js');
 assert.deepEqual(decode('COLONY|1|15|CELESTIAL_HISTORY|4|12'),
   {tick:15,type:'CELESTIAL_HISTORY',first:4,last:12});
 assert.throws(()=>decode('COLONY|1|15|CELESTIAL_HISTORY|20|10'));
 assert.throws(()=>decode('COLONY|1|15|CELESTIAL_HISTORY|0|10'));
});

test('native persisted celestial bodies have safe cosmic identity and coordinates',()=>{
 const {decode}=require('../viewer/colony.js');
 const row=decode('COLONY|1|7|CELESTIAL_BODY|4294967297|0|0|0|0|0|0|700000000000');
 assert.equal(row.id,4294967297);
 assert.equal(row.kind,0);
 assert.equal(row.radius,700000000000);
 const planet=decode('COLONY|1|7|CELESTIAL_BODY|4294967298|4294967297|1|1500|150000000000000|0|0|6000000000');
 assert.equal(planet.kind,1);
 assert.equal(planet.parent,4294967297);
 assert.equal(planet.x,150000000000000);
 for(const bad of ['COLONY|1|7|CELESTIAL_BODY|3|0|1|1500|0|0|0|6000000000',
   'COLONY|1|7|CELESTIAL_BODY|3|1|1|1500|-0|0|0|6000000000',
   'COLONY|1|7|CELESTIAL_BODY|3|1|7|1500|0|0|0|6000000000'])
   assert.throws(()=>decode(bad));
});
