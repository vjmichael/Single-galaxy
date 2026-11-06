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
  assert.match(host,/execute_orbital_shuttle\(/);
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

test('PLAY-13D.4G real intercolony carrier telemetry is strict',()=>{
  const planned=decode('COLONY|1|20|INTERFREIGHT_ITEM|77003|3|4|NUTRIENTS|200|0|0');
  assert.equal(planned.stage,0);
  assert.equal(decode('COLONY|1|20|INTERFREIGHT_PLANNED|77003|3|4|444').generation,444);
  const aboard=decode('COLONY|1|20|INTERFREIGHT_ITEM|77003|3|4|NUTRIENTS|200|1|0');
  assert.equal(aboard.carrier,3);
  assert.equal(aboard.destination,4);
  assert.equal(aboard.kg,200);
  assert.equal(aboard.stage,1);
  const delivered=decode('COLONY|1|20|INTERFREIGHT_ITEM|77003|3|4|NUTRIENTS|200|2|99021');
  assert.equal(delivered.receipt,99021);
  assert.equal(delivered.stage,2);
  assert.equal(decode('COLONY|1|20|INTERFREIGHT_LOADED|77003|3|4|445').generation,445);
  assert.equal(decode('COLONY|1|20|INTERFREIGHT_DEPARTED|77003|99021|3|4').request,99021);
  assert.equal(decode('COLONY|1|20|INTERFREIGHT_DELIVERED|77003|447').order,77003);
  assert.equal(decode('COLONY|1|20|INTERFREIGHT_DONE|1').count,1);
  for(const bad of [
    'COLONY|1|20|INTERFREIGHT_ITEM|77003|3|4|NUTRIENTS|200|2|0',
    'COLONY|1|20|INTERFREIGHT_ITEM|77003|3|4|NUTRIENTS|200|1|99021',
    'COLONY|1|20|INTERFREIGHT_ITEM|77003|3|3|NUTRIENTS|200|1|0',
    'COLONY|1|20|INTERFREIGHT_ITEM|77003|3|4|GOLD|200|1|0',
    'COLONY|1|20|INTERFREIGHT_ITEM|77003|3|4|NUTRIENTS|1001|1|0',
    'COLONY|1|20|INTERFREIGHT_LOADED|77003|3|4|0',
    'COLONY|1|20|INTERFREIGHT_DEPARTED|77003|0|3|4',
    'COLONY|1|20|INTERFREIGHT_DELIVERED|77003|0',
    'COLONY|1|20|INTERFREIGHT_DONE|65'
  ])assert.throws(()=>decode(bad),Error,bad);
  const ui=fs.readFileSync(path.join(root,'viewer/colony.js'),'utf8');
  const relay=fs.readFileSync(path.join(root,'live/interactive_relay.py'),'utf8');
  const host=fs.readFileSync(path.join(root,'src/colony_live_host.cpp'),'utf8');
  for(const action of ['colonyFreightLoad','colonyFreightDispatch',
                        'colonyFreightDeliver','colonyFreightStatus',
                        'colonyLocalWorkPostToColony']){
    assert.match(ui,new RegExp(action));
    assert.match(relay,new RegExp(action));
  }
  assert.match(ui,/Existing carrier ship ID/);
  assert.match(host,/deliver_intercolony_freight/);
  assert.match(host,/begin_celestial_transit/);
});

test('SHIP-01A native shipyard construction progress and UI are bounded by receipts',()=>{
  const assigned=decode('COLONY|1|20|SHIPYARD_ASSIGNED|99001|124');
  assert.equal(assigned.generation,124);
  const started=decode('COLONY|1|20|SHIPYARD_STARTED|99001|123');
  assert.equal(started.order,99001);
  const awaiting=decode('COLONY|1|20|SHIPYARD_ITEM|99001|1|101|102|0|8|0|123');
  assert.equal(awaiting.progress,0);
  assert.equal(awaiting.required,8);
  assert.equal(decode('COLONY|1|28|SHIPYARD_ITEM|99001|1|101|102|8|8|2|131').progress,8);
  assert.equal(decode('COLONY|1|20|SHIPYARD_DONE|1').count,1);
  for(const bad of [
    'COLONY|1|20|SHIPYARD_ITEM|99001|1|101|102|9|8|0|123',
    'COLONY|1|20|SHIPYARD_ITEM|99001|1|101|102|3|9|0|123',
    'COLONY|1|20|SHIPYARD_ITEM|99001|1|101|101|3|8|0|123',
    'COLONY|1|20|SHIPYARD_ITEM|0|1|101|102|3|8|0|123',
    'COLONY|1|20|SHIPYARD_ITEM|99001|1|101|102|3|8|0|0',
    'COLONY|1|20|SHIPYARD_STARTED|0|123',
    'COLONY|1|20|SHIPYARD_STARTED|99001|0',
    'COLONY|1|20|SHIPYARD_DONE|65'
  ])assert.throws(()=>decode(bad),Error,bad);
  const ui=fs.readFileSync(path.join(root,'viewer/colony.js'),'utf8');
  const relay=fs.readFileSync(path.join(root,'live/interactive_relay.py'),'utf8');
  const host=fs.readFileSync(path.join(root,'src/colony_live_host.cpp'),'utf8');
  assert.match(ui,/Build orbital shipyard/);
  assert.match(ui,/colonyShipyardBuild/);
  assert.match(ui,/colonyShipyardAssign/);
  assert.match(ui,/colonyShipyardStatus/);
  assert.match(relay,/SHIPYARD_BUILD/);
  assert.match(relay,/SHIPYARD_ASSIGN/);
  assert.match(relay,/SHIPYARD_STATUS/);
  assert.match(host,/start_shipyard_construction/);
  assert.match(host,/shipyard_constructions/);
});

test('SHIP-01B native real freighter hull manufacturing events are bounded',()=>{
  const queued=decode('COLONY|1|20|FREIGHTER_QUEUED|92001|91001|123');
  assert.equal(queued.order,92001);
  const awaiting=decode('COLONY|1|20|FREIGHTER_ITEM|92001|91001|0|12|0|123');
  assert.equal(awaiting.hull,0);
  assert.equal(awaiting.required,12);
  const launched=decode('COLONY|1|35|FREIGHTER_ITEM|92001|91001|12|12|8|155');
  assert.equal(launched.hull,8);
  assert.equal(launched.progress,12);
  assert.equal(decode('COLONY|1|35|FREIGHTER_LAUNCHED|92001|8|155').hull,8);
  assert.equal(decode('COLONY|1|35|FREIGHTER_DONE|1').count,1);
  for(const bad of [
    'COLONY|1|20|FREIGHTER_ITEM|92001|91001|12|12|0|123',
    'COLONY|1|20|FREIGHTER_ITEM|92001|91001|11|12|8|123',
    'COLONY|1|20|FREIGHTER_ITEM|92001|91001|13|12|8|123',
    'COLONY|1|20|FREIGHTER_ITEM|92001|91001|11|10|0|123',
    'COLONY|1|20|FREIGHTER_ITEM|91001|91001|0|12|0|123',
    'COLONY|1|20|FREIGHTER_QUEUED|92001|91001|0',
    'COLONY|1|20|FREIGHTER_LAUNCHED|92001|0|123',
    'COLONY|1|20|FREIGHTER_DONE|65'
  ])assert.throws(()=>decode(bad),Error,bad);
  const ui=fs.readFileSync(path.join(root,'viewer/colony.js'),'utf8');
  const relay=fs.readFileSync(path.join(root,'live/interactive_relay.py'),'utf8');
  const host=fs.readFileSync(path.join(root,'src/colony_live_host.cpp'),'utf8');
  assert.match(ui,/Manufacture cargo freighter/);
  assert.match(ui,/colonyFreighterBuild/);
  assert.match(ui,/colonyFreighterStatus/);
  assert.match(relay,/FREIGHTER_BUILD/);
  assert.match(relay,/FREIGHTER_STATUS/);
  assert.match(host,/queue_shipyard_freighter/);
  assert.match(host,/freighter_build_orders/);
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

test('SHIP-01B.2 commissioning receipt is bounded, authenticated and visible',()=>{
  const c=decode('COLONY|1|42|FREIGHTER_COMMISSIONED|92001|8|103|104|20|155');
  assert.equal(c.hull,8);
  assert.equal(c.firstNpc,103);
  assert.equal(c.secondNpc,104);
  assert.equal(c.provisionsKg,20);
  assert.equal(c.generation,155);
  for(const bad of [
    'COLONY|1|42|FREIGHTER_COMMISSIONED|92001|8|103|103|20|155',
    'COLONY|1|42|FREIGHTER_COMMISSIONED|92001|8|103|104|9|155',
    'COLONY|1|42|FREIGHTER_COMMISSIONED|92001|8|103|104|1001|155',
    'COLONY|1|42|FREIGHTER_COMMISSIONED|92001|0|103|104|20|155',
    'COLONY|1|42|FREIGHTER_COMMISSIONED|92001|8|103|104|20|0',
    'COLONY|1|42|FREIGHTER_COMMISSIONED|92001|8|103|104|20|155|extra'
  ])assert.throws(()=>decode(bad),Error,bad);
  const ui=fs.readFileSync(path.join(root,'viewer/colony.js'),'utf8');
  const relay=fs.readFileSync(path.join(root,'live/interactive_relay.py'),'utf8');
  const host=fs.readFileSync(path.join(root,'src/colony_live_host.cpp'),'utf8');
  assert.match(ui,/Commission freighter with real crew/);
  assert.match(ui,/colonyFreighterCommission/);
  assert.match(relay,/FREIGHTER_COMMISSION/);
  assert.match(host,/commission_shipyard_freighter/);
});

test('SHIP-01C commissioned freighter cockpit reads real physical receipts only',()=>{
  const e=decode('COLONY|1|43|FREIGHTER_PILOT_ITEM|92001|8|90|-1000|2000|4294967300|2|237|238|239|180');
  assert.equal(e.order,92001);
  assert.equal(e.hull,8);
  assert.equal(e.x,-1000);
  assert.equal(e.body,4294967300);
  assert.equal(e.crew,2);
  assert.equal(e.food,237);
  const thrust=decode('COLONY|1|44|FREIGHTER_PILOT_QUEUED|92001|8|900000000123|181');
  assert.equal(thrust.command,900000000123);
  const flight=decode('COLONY|1|45|FREIGHTER_TRANSIT_STARTED|92001|8|900000000124|4294967300|182');
  assert.equal(flight.body,4294967300);
  for(const bad of [
    'COLONY|1|43|FREIGHTER_PILOT_ITEM|92001|8|360|-1000|2000|4294967300|2|237|238|239|180',
    'COLONY|1|43|FREIGHTER_PILOT_ITEM|92001|8|90|+1000|2000|4294967300|2|237|238|239|180',
    'COLONY|1|43|FREIGHTER_PILOT_ITEM|92001|8|90|-0|2000|4294967300|2|237|238|239|180',
    'COLONY|1|43|FREIGHTER_PILOT_ITEM|92001|8|90|-1000|2000|4294967300|0|237|238|239|180',
    'COLONY|1|43|FREIGHTER_PILOT_ITEM|92001|8|90|-1000|2000|4294967300|2|237|238|239|0',
    'COLONY|1|44|FREIGHTER_PILOT_QUEUED|92001|8|0|181',
    'COLONY|1|44|FREIGHTER_PILOT_QUEUED|92001|8|900000000123|0',
    'COLONY|1|45|FREIGHTER_TRANSIT_STARTED|92001|8|900000000124|0|182'
  ])assert.throws(()=>decode(bad),Error,bad);
  const ui=fs.readFileSync(path.join(root,'viewer/colony.js'),'utf8');
  const relay=fs.readFileSync(path.join(root,'live/interactive_relay.py'),'utf8');
  const host=fs.readFileSync(path.join(root,'src/colony_live_host.cpp'),'utf8');
  assert.match(ui,/colonyFreighterThrust/);
  assert.match(ui,/colonyFreighterYaw/);
  assert.match(ui,/colonyFreighterTravel/);
  assert.match(host,/FREIGHTER_TRAVEL/);
  assert.match(host,/manufactured_order_id!=order_id/);
  assert.match(relay,/FREIGHTER_PILOT_STATUS/);
});

test('SHIP-01D owned fleet receipts enforce home provenance, readiness and strict selected cockpit',()=>{
  assert.equal(decode('COLONY|1|52|FLEET_RESET').type,'FLEET_RESET');
  const home=decode('COLONY|1|52|FLEET_ITEM|1|0|1|1|20|0|1800|1700|1900');
  assert.equal(home.ship,1);assert.equal(home.order,0);assert.equal(home.home,true);
  assert.equal(home.ready,true);
  const manufacturing=decode('COLONY|1|52|FLEET_ITEM|12|92001|0|0|0|0|0|0|0');
  assert.equal(manufacturing.ready,false);
  const crewed=decode('COLONY|1|53|FLEET_ITEM|12|92001|0|1|2|90012|198|199|200');
  assert.equal(crewed.order,92001);assert.equal(crewed.crew,2);
  assert.equal(decode('COLONY|1|53|FLEET_DONE|2').count,2);
  const selected=decode('COLONY|1|54|FLEET_SELECTED|12|1|300');
  assert.equal(selected.ship,12);assert.equal(selected.home,1);
  assert.equal(decode('COLONY|1|55|FLEET_FALLBACK|1|1|301').ship,1);
  const pilot=decode('COLONY|1|56|FLEET_PILOT_ITEM|12|345|-1000|2000|90012|2|196|197|198|302');
  assert.equal(pilot.heading,345);assert.equal(pilot.x,-1000);
  assert.equal(pilot.oxygen,198);
  for(const bad of [
    'COLONY|1|52|FLEET_RESET|extra',
    'COLONY|1|52|FLEET_ITEM|1|0|0|1|20|0|1800|1700|1900',
    'COLONY|1|52|FLEET_ITEM|12|92001|1|1|2|0|1800|1700|1900',
    'COLONY|1|52|FLEET_ITEM|12|92001|0|1|0|0|0|0|0',
    'COLONY|1|52|FLEET_ITEM|12|92001|0|3|2|0|0|0|0',
    'COLONY|1|53|FLEET_DONE|10001',
    'COLONY|1|54|FLEET_SELECTED|0|1|300',
    'COLONY|1|54|FLEET_SELECTED|12|1|0',
    'COLONY|1|55|FLEET_FALLBACK|12|1|301',
    'COLONY|1|56|FLEET_PILOT_ITEM|12|360|-1000|2000|90012|2|196|197|198|302',
    'COLONY|1|56|FLEET_PILOT_ITEM|12|345|-0|2000|90012|2|196|197|198|302',
    'COLONY|1|56|FLEET_PILOT_ITEM|12|345|-1000|2000|90012|0|196|197|198|302'
  ])assert.throws(()=>decode(bad),Error,bad);
  assert.equal(decode('COLONY|1|57|ERROR|FLEET_SELECTION_DENIED').error,
    'FLEET_SELECTION_DENIED');
  const ui=fs.readFileSync(path.join(root,'viewer/colony.js'),'utf8');
  const map=fs.readFileSync(path.join(root,'viewer/viewer.js'),'utf8');
  const interactive=fs.readFileSync(path.join(root,'viewer/interactive.js'),'utf8');
  const host=fs.readFileSync(path.join(root,'src/colony_live_host.cpp'),'utf8');
  const relay=fs.readFileSync(path.join(root,'live/interactive_relay.py'),'utf8');
  assert.match(ui,/Your Fleet · Active Ship/);
  assert.match(ui,/colonyFleetSelect/);
  assert.match(ui,/pilotSaveKey/);
  assert.match(map,/GalaxyActivePilotedShipId/);
  assert.match(interactive,/colonyFleetStatus/);
  assert.match(host,/FLEET_SELECT/);
  assert.match(host,/c.actor=fleet.piloted\(\)/);
  assert.match(host,/bridge.set_focus_ship\(fleet.piloted\(\)\)/);
  assert.match(relay,/FLEET_STATUS/);
});

test('SHIP-01E fleet freight assignment reuses canonical work board without phantom cargo',()=>{
  const assignment=decode('COLONY|1|75|FLEET_WORK_ASSIGNED|77007|12|3|151');
  assert.equal(assignment.order,77007);
  assert.equal(assignment.carrier,12);
  assert.equal(assignment.destination,3);
  assert.equal(assignment.generation,151);
  assert.equal(decode('COLONY|1|76|ERROR|FLEET_WORK_DENIED').error,
    'FLEET_WORK_DENIED');
  const stage=decode('COLONY|1|76|INTERFREIGHT_ITEM|77007|12|3|NUTRIENTS|100|0|0');
  assert.equal(stage.stage,0);
  assert.equal(stage.carrier,assignment.carrier);
  const finished=decode('COLONY|1|91|INTERFREIGHT_ITEM|77007|12|3|NUTRIENTS|100|2|99023');
  assert.equal(finished.stage,2);
  assert.equal(finished.receipt,99023);
  for(const bad of [
    'COLONY|1|75|FLEET_WORK_ASSIGNED|0|12|3|151',
    'COLONY|1|75|FLEET_WORK_ASSIGNED|77007|12|12|151',
    'COLONY|1|75|FLEET_WORK_ASSIGNED|77007|12|3|0',
    'COLONY|1|75|FLEET_WORK_ASSIGNED|77007|12|3|151|extra',
    'COLONY|1|76|ERROR|FAKE_FLEET_DELIVERY'
  ])assert.throws(()=>decode(bad),Error,bad);
  const ui=fs.readFileSync(path.join(root,'viewer/colony.js'),'utf8');
  const host=fs.readFileSync(path.join(root,'src/colony_live_host.cpp'),'utf8');
  const relay=fs.readFileSync(path.join(root,'live/interactive_relay.py'),'utf8');
  const policy=fs.readFileSync(path.join(root,'include/galaxy/FleetSelection.hpp'),'utf8');
  assert.match(ui,/Assign NPC freighter delivery/);
  assert.match(ui,/colonyFleetWorkPost/);
  assert.match(ui,/FLEET_WORK_ASSIGNED/);
  assert.match(host,/post_intercolony_work_order/);
  assert.match(host,/FLEET_WORK_POST/);
  assert.match(host,/leg.carrier_ship==fleet.piloted\(\)/);
  assert.match(relay,/FLEET_WORK_POST/);
  assert.match(policy,/fleet_freight_eligible/);
});
