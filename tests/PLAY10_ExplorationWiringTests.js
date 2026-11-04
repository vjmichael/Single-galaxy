const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const {decode}=require('../viewer/colony.js');
test('strict native first-discovery telemetry accepts coordinates and persisted tick',()=>{
 assert.deepEqual(decode('COLONY|1|17|DISCOVERY|42|1|-2000|5000|9|1'),
 {tick:17,type:'DISCOVERY',site:42,system:1,x:-2000,y:5000,firstTick:9,geological:true});
 for(const value of [
  'COLONY|1|17|DISCOVERY|0|1|-2000|5000|9|1',
  'COLONY|1|17|DISCOVERY|42|1|-2000|5000|18|1',
  'COLONY|1|17|DISCOVERY|42|1|-2000|5000|9|2',
  'COLONY|1|17|DISCOVERY|42|1|-0|5000|9|1',
  'COLONY|1|17|DISCOVERY|42|1|-2000|5000|9'
 ])assert.throws(()=>decode(value),value);
});
test('exploration browser view calls native committed scan and shows only decoded history',()=>{
 const html=read('viewer/interactive.html'),client=read('viewer/interactive.js');
 const colony=read('viewer/colony.js'),relay=read('live/interactive_relay.py');
 const native=read('src/colony_live_host.cpp');
 for(const id of ['explorationScan','explorationSummary','explorationResults'])
  assert.ok(html.includes('id="'+id+'"'),id+' missing');
 assert.ok(html.includes('<script src="exploration-journal.js"></script>'));
 assert.ok(relay.includes("'/viewer/exploration-journal.js':'viewer/exploration-journal.js'"));
 assert.ok(client.includes("el('explorationScan').addEventListener('click',()=>command('colonyScan'))"));
 assert.ok(native.includes('c.action=Action::SurveyLocal'));
 assert.ok(native.includes('w.discoveries()'));
 assert.ok(colony.includes("if(e.type==='DISCOVERY')"));
});
test('scanning is not automatic at login; no browser-side persistent discovery write',()=>{
 const native=read('src/colony_live_host.cpp');
 assert.ok(!native.includes('status(world.world(),ship);scan(world.world(),ship);'));
 const client=read('viewer/interactive.js');
 const journal=read('viewer/exploration-journal.js');
 for(const forbidden of ['localStorage','indexedDB','fetch(','.setItem('])
  assert.ok(!journal.includes(forbidden),'fake client persistence: '+forbidden);
 assert.ok(client.includes('window.GalaxyColonyMode=colonyMode'));
});
