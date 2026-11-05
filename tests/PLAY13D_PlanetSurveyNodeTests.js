const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {decode,makeShuttleRosterTracker}=require('../viewer/colony.js');
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
test('PLAY-13D planetary site reports exact nutrient inventory without inferred extraction',()=>{
  const stocked=decode('COLONY|1|13|PLANET_SITE_STATUS|4294967300|17|36|3|240|240|2|35|28|26|42|43|3|100|5|1|1000|0|750');
  assert.equal(stocked.nutrients,750);
  assert.equal(stocked.policy,3);
  const legacy=decode('COLONY|1|13|PLANET_SITE_STATUS|4294967300|17|36|3|240|240|2|35|28|26|42|43|3|100|5|1|1000|0');
  assert.equal(legacy.nutrients,0);
  for(const bad of [
    'COLONY|1|13|PLANET_SITE_STATUS|4294967300|17|36|3|240|240|2|35|28|26|42|43|3|100|5|1|1000|0|1000001',
    'COLONY|1|13|PLANET_SITE_STATUS|4294967300|17|36|3|240|240|2|35|28|26|42|43|3|100|5|1|1000|0|-1'
  ])assert.throws(()=>decode(bad),Error,bad);
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

test('orbital shuttle telemetry, receipts and UI are bounded by native schema',()=>{
  const report=decode('COLONY|1|12|PLANET_SHUTTLE_STATUS|4294967300|17|36|1|1|42|149|50|45|101|101|0|77');
  assert.equal(report.phase,1);
  assert.equal(report.pilot,42);
  assert.equal(report.water,149);
  assert.equal(report.request,101);
  const empty=decode('COLONY|1|12|PLANET_SHUTTLE_STATUS|4294967300|17|36|0|0|0|0|0|0|0|0|0|77');
  assert.equal(empty.present,false);
  const receipt=decode('COLONY|1|12|PLANET_SHUTTLE_COMMITTED|4294967300|17|36|DESCEND|88');
  assert.equal(receipt.operation,'DESCEND');
  for(const raw of [
    'COLONY|1|12|PLANET_SHUTTLE_STATUS|4294967300|17|36|1|4|42|149|50|45|101|101|0|77',
    'COLONY|1|12|PLANET_SHUTTLE_STATUS|4294967300|17|36|1|1|42|1000|50|45|101|101|0|77',
    'COLONY|1|12|PLANET_SHUTTLE_STATUS|4294967300|17|36|1|1|0|149|50|45|101|101|0|77',
    'COLONY|1|12|PLANET_SHUTTLE_STATUS|4294967300|17|36|1|1|42|149|50|0|101|101|0|77',
    'COLONY|1|12|PLANET_SHUTTLE_STATUS|4294967300|17|36|0|1|42|149|50|45|101|101|0|77',
    'COLONY|1|12|PLANET_SHUTTLE_COMMITTED|4294967300|17|36|FREE|88',
    'COLONY|1|12|PLANET_SHUTTLE_COMMITTED|4294967300|17|36|LOAD|0'
  ])assert.throws(()=>decode(raw),Error,raw);
  const ui=fs.readFileSync(path.join(root,'viewer/colony.js'),'utf8');
  const relay=fs.readFileSync(path.join(root,'live/interactive_relay.py'),'utf8');
  const host=fs.readFileSync(path.join(root,'src/colony_live_host.cpp'),'utf8');
  assert.match(ui,/colonyPlanetShuttle/);
  assert.match(ui,/Descend to surface/);
  assert.match(relay,/PLANET_SHUTTLE/);
  assert.match(host,/launch_planetary_shuttle/);
});

test('PLAY-13D recovered nutrients remain bounded native cargo',()=>{
  const extended=decode('COLONY|1|12|PLANET_SHUTTLE_STATUS|4294967300|17|36|1|2|42|149|50|0|0|101|0|77|500');
  assert.equal(extended.nutrients,500);
  assert.equal(extended.phase,2);
  const legacy=decode('COLONY|1|12|PLANET_SHUTTLE_STATUS|4294967300|17|36|1|2|42|149|50|0|0|101|0|77');
  assert.equal(legacy.nutrients,0);
  for(const bad of [
    'COLONY|1|12|PLANET_SHUTTLE_STATUS|4294967300|17|36|1|2|42|600|100|0|0|101|0|77|400',
    'COLONY|1|12|PLANET_SHUTTLE_STATUS|4294967300|17|36|1|2|42|149|50|0|0|101|0|77|1001',
    'COLONY|1|12|PLANET_SHUTTLE_STATUS|4294967300|17|36|0|0|0|0|0|0|0|0|0|77|1'
  ])assert.throws(()=>decode(bad),Error,bad);
  const ui=fs.readFileSync(path.join(root,'viewer/colony.js'),'utf8');
  const relay=fs.readFileSync(path.join(root,'live/interactive_relay.py'),'utf8');
  const host=fs.readFileSync(path.join(root,'src/colony_live_host.cpp'),'utf8');
  assert.match(ui,/Nutrients \(planetary output\)/);
  assert.match(relay,/resource not in \('FOOD','WATER','NUTRIENTS'\)/);
  assert.match(host,/ShuttleCargo::Nutrients/);
});

test('PLAY-13D.4C.7 shuttle manifest carries finite named NPC IDs',()=>{
  const named=decode('COLONY|1|14|PLANET_SHUTTLE_PASSENGER|4294967300|17|36|84');
  assert.equal(named.npc,84);
  const done=decode('COLONY|1|14|PLANET_SHUTTLE_MANIFEST_DONE|4294967300|17|36|1');
  assert.equal(done.count,1);
  assert.equal(decode('COLONY|1|14|PLANET_SHUTTLE_COMMITTED|4294967300|17|36|BOARD|33').operation,'BOARD');
  assert.equal(decode('COLONY|1|14|PLANET_SHUTTLE_COMMITTED|4294967300|17|36|RELEASE|34').operation,'RELEASE');
  for(const raw of [
    'COLONY|1|14|PLANET_SHUTTLE_PASSENGER|4294967300|17|36|0',
    'COLONY|1|14|PLANET_SHUTTLE_MANIFEST_DONE|4294967300|17|36|4',
    'COLONY|1|14|PLANET_SHUTTLE_PASSENGER|4294967300|17|36',
    'COLONY|1|14|PLANET_SHUTTLE_MANIFEST_DONE|4294967300|36|36|1'
  ])assert.throws(()=>decode(raw),Error,raw);
});

test('PLAY-13D.4C.8 surface visitors have canonical named custody frames',()=>{
  const npc=decode('COLONY|1|14|PLANET_SHUTTLE_VISITOR|4294967300|17|36|84');
  assert.equal(npc.npc,84);
  const complete=decode('COLONY|1|14|PLANET_SHUTTLE_VISITORS_DONE|4294967300|17|36|1');
  assert.equal(complete.count,1);
  assert.equal(decode('COLONY|1|14|PLANET_SHUTTLE_COMMITTED|4294967300|17|36|DISEMBARK|35').operation,'DISEMBARK');
  assert.equal(decode('COLONY|1|14|PLANET_SHUTTLE_COMMITTED|4294967300|17|36|REBOARD|36').operation,'REBOARD');
  for(const raw of [
    'COLONY|1|14|PLANET_SHUTTLE_VISITOR|4294967300|17|36|0',
    'COLONY|1|14|PLANET_SHUTTLE_VISITORS_DONE|4294967300|17|36|65',
    'COLONY|1|14|PLANET_SHUTTLE_VISITOR|4294967300|17|36',
    'COLONY|1|14|PLANET_SHUTTLE_VISITORS_DONE|4294967300|36|36|1'
  ])assert.throws(()=>decode(raw),Error,raw);
});

test('PLAY-13D.4C.8 work duty is not implied by planetary disembarkation',()=>{
  assert.equal(decode('COLONY|1|14|PLANET_SHUTTLE_WORKER|4294967300|17|36|84').npc,84);
  assert.equal(decode('COLONY|1|14|PLANET_SHUTTLE_WORKERS_DONE|4294967300|17|36|1').count,1);
  assert.equal(decode('COLONY|1|14|PLANET_SHUTTLE_COMMITTED|4294967300|17|36|DUTY_ON|37').operation,'DUTY_ON');
  assert.equal(decode('COLONY|1|14|PLANET_SHUTTLE_COMMITTED|4294967300|17|36|DUTY_OFF|38').operation,'DUTY_OFF');
  for(const raw of [
    'COLONY|1|14|PLANET_SHUTTLE_WORKER|4294967300|17|36|0',
    'COLONY|1|14|PLANET_SHUTTLE_WORKERS_DONE|4294967300|17|36|65',
    'COLONY|1|14|PLANET_SHUTTLE_WORKER|4294967300|17|36',
  ])assert.throws(()=>decode(raw),Error,raw);
});

test('PLAY-13D.4F local work-board contract telemetry uses real NPC delivery stages',()=>{
  const posted=decode('COLONY|1|20|LOCAL_WORK_POSTED|77003|443');
  assert.equal(posted.order,77003);
  assert.equal(posted.generation,443);
  const listed=decode('COLONY|1|20|LOCAL_WORK_ITEM|77003|4294967300|17|36|NUTRIENTS|500|4|42');
  assert.equal(listed.order,77003);
  assert.equal(listed.kg,500);
  assert.equal(listed.stage,4);
  assert.equal(listed.pilot,42);
  assert.equal(decode('COLONY|1|20|LOCAL_WORK_DONE|1').count,1);
  assert.equal(decode('COLONY|1|20|LOCAL_WORK_CANCELLED|77004|445').order,77004);
  for(const raw of [
    'COLONY|1|20|LOCAL_WORK_ITEM|77003|4294967300|17|36|NUTRIENTS|500|7|42',
    'COLONY|1|20|LOCAL_WORK_ITEM|77003|4294967300|17|36|NUTRIENTS|500|2|0',
    'COLONY|1|20|LOCAL_WORK_ITEM|77003|4294967300|17|36|GOLD|500|1|42',
    'COLONY|1|20|LOCAL_WORK_ITEM|77003|4294967300|17|36|NUTRIENTS|1001|1|42',
    'COLONY|1|20|LOCAL_WORK_POSTED|0|443',
    'COLONY|1|20|LOCAL_WORK_DONE|65'
  ]) assert.throws(()=>decode(raw),Error,raw);
  const ui=fs.readFileSync(path.join(root,'viewer/colony.js'),'utf8');
  const relay=fs.readFileSync(path.join(root,'live/interactive_relay.py'),'utf8');
  const host=fs.readFileSync(path.join(root,'src/colony_live_host.cpp'),'utf8');
  assert.match(ui,/Queue resource pickup for NPC pilot/);
  assert.match(ui,/Queued — waiting for available shuttle, fuel, and stock/);
  assert.match(ui,/Unloading into colony ship/);
  assert.match(ui,/colonyLocalWorkPost/);
  assert.match(ui,/colonyLocalWorkStatus/);
  assert.match(ui,/colonyLocalWorkCancel/);
  assert.match(relay,/LOCAL_WORK_POST/);
  assert.match(relay,/LOCAL_WORK_CANCEL/);
  assert.match(host,/process_local_delivery_board|local_delivery_board/);
  assert.match(host,/else if\(count<64\)emit\(id,job\)/);
  assert.match(host,/std::sort\(history.begin\(\),history.end\(\)/);
  assert.match(ui,/workList.replaceChildren\(\)/);
});

test('PLAY-13D shuttle UI separates booked passengers, landed visitors and temporary duty',()=>{
  const t=makeShuttleRosterTracker();
  const e=(type,value)=>decode(
    'COLONY|1|14|'+type+'|4294967300|17|36|'+value);
  assert.equal(t.accept(e('PLANET_SHUTTLE_PASSENGER',41)),null);
  assert.equal(t.accept(e('PLANET_SHUTTLE_PASSENGER',42)),null);
  assert.deepEqual(t.accept(e('PLANET_SHUTTLE_MANIFEST_DONE',2)),
    {kind:'passengers',ids:[41,42]});
  assert.deepEqual(t.snapshot(),{passengers:[41,42],visitors:[],workers:[]});
  assert.equal(t.accept(e('PLANET_SHUTTLE_VISITOR',84)),null);
  assert.deepEqual(t.accept(e('PLANET_SHUTTLE_VISITORS_DONE',1)),
    {kind:'visitors',ids:[84]});
  assert.equal(t.accept(e('PLANET_SHUTTLE_WORKER',84)),null);
  assert.deepEqual(t.accept(e('PLANET_SHUTTLE_WORKERS_DONE',1)),
    {kind:'workers',ids:[84]});
  assert.deepEqual(t.snapshot(),{passengers:[41,42],visitors:[84],workers:[84]});
  // An empty manifest replaces the previous view; it never appends stale IDs.
  assert.deepEqual(t.accept(e('PLANET_SHUTTLE_MANIFEST_DONE',0)),
    {kind:'passengers',ids:[]});
  assert.deepEqual(t.snapshot().passengers,[]);
  assert.deepEqual(t.snapshot().visitors,[84]);
  // A separate world/region panel starts with no stale transport identities.
  assert.deepEqual(makeShuttleRosterTracker().snapshot(),
    {passengers:[],visitors:[],workers:[]});
});

test('PLAY-13D shuttle UI rejects incomplete, duplicate and unsorted native manifests',()=>{
  const e=(type,value)=>decode(
    'COLONY|1|14|'+type+'|4294967300|17|36|'+value);
  const t=makeShuttleRosterTracker();
  t.accept(e('PLANET_SHUTTLE_PASSENGER',13));
  assert.throws(()=>t.accept(e('PLANET_SHUTTLE_PASSENGER',13)),/Noncanonical/);
  assert.throws(()=>t.accept(e('PLANET_SHUTTLE_MANIFEST_DONE',2)),/Incomplete/);
  assert.deepEqual(t.snapshot().passengers,[]);
  t.accept(e('PLANET_SHUTTLE_VISITOR',40));
  assert.throws(()=>t.accept(e('PLANET_SHUTTLE_VISITOR',39)),/Noncanonical/);
  assert.deepEqual(t.accept(e('PLANET_SHUTTLE_VISITORS_DONE',1)),
    {kind:'visitors',ids:[40]});
  t.accept(e('PLANET_SHUTTLE_WORKER',42));
  assert.throws(()=>t.accept(e('PLANET_SHUTTLE_WORKERS_DONE',1)),/no physically disembarked visitor/);
  assert.deepEqual(t.snapshot().workers,[]);
});

test('PLAY-13D shuttle UI exposes named NPC selection instead of mislabeling all as booked',()=>{
  const ui=fs.readFileSync(path.join(root,'viewer/colony.js'),'utf8');
  assert.match(ui,/Booked aboard \(max 3\)/);
  assert.match(ui,/Visitors on surface/);
  assert.match(ui,/Visitors on temporary duty/);
  assert.match(ui,/Select NPC.*for shuttle order/);
  assert.match(ui,/makeShuttleRosterTracker/);
  assert.doesNotMatch(ui,/dataset\.shuttlePassengers/);
});
