'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {decode}=require('../viewer/colony.js');
const packet=x=>`COLONY|1|200|${x}`;
test('B7.0k-2 reclaimer rejects inconsistent physical mass and service counts',()=>{
 const r=decode(packet('RECLAIMER|7|27|14|3|6|6|3'));
 assert.deepEqual([r.id,r.reclaimed,r.generated,r.sludge,r.carbon,r.cycles,r.continuity],[7,27,14,3,6,6,3]);
 for(const bad of [
  'RECLAIMER|0|27|14|3|6|6|3',
  'RECLAIMER|7|28|14|3|6|6|3',
  'RECLAIMER|7|27|15|3|6|6|3',
  'RECLAIMER|7|27|14|3|6|6|7',
  'RECLAIMER|7|27|14|3|6|6',
  'RECLAIMER|7|27|14|3|6|6|3|MORE',
  'RECLAIMER|7|-1|14|3|6|6|3'
 ])assert.throws(()=>decode(packet(bad)),bad);
});
test('B7.0k-2 recent throughput is not independent water renewal',()=>{
 const a=decode(packet('LIFE_RECOVERY|2|2|2|60|48|48|48|3|12|1|0'));
 assert.equal(a.covered,1);assert.equal(a.renewable,0);
 const b=decode(packet('LIFE_RECOVERY|2|1|1|12|10|48|48|2|3|0|0'));
 assert.equal(b.covered,0);
 for(const bad of [
  'LIFE_RECOVERY|1|2|1|20|20|10|10|2|2|1|0',
  'LIFE_RECOVERY|2|2|1|20|20|10|10|2|2|1|0',
  'LIFE_RECOVERY|2|2|2|20|20|30|30|2|2|1|0',
  'LIFE_RECOVERY|2|2|2|20|20|20|20|2|2|1|1',
  'LIFE_RECOVERY|2|2|2|20|20|20|20|2|2|2|0'
 ])assert.throws(()=>decode(packet(bad)),bad);
});
test('B7.0k-2 UI and relay expose only player-owned bounded local construction',()=>{
 const root=path.join(__dirname,'..');
 const html=fs.readFileSync(path.join(root,'viewer/interactive.html'),'utf8');
 const ui=fs.readFileSync(path.join(root,'viewer/colony.js'),'utf8');
 const relay=fs.readFileSync(path.join(root,'live/interactive_relay.py'),'utf8');
 const host=fs.readFileSync(path.join(root,'src/colony_live_host.cpp'),'utf8');
 for(const id of ['colonyReclaimerOutpost','colonyReclaimerBuild','reclaimerReadiness','reclaimerTelemetry'])
  assert.ok(html.includes(`id="${id}"`),id);
 assert.ok(ui.includes('colonyReclaimerBuild'));
 assert.ok(host.includes('RECLAIMER_BUILD') && host.includes('LIFE_RECOVERY|'));
 assert.ok(relay.includes("type(dest)!=int") && relay.includes('colonyReclaimerBuild'));
 assert.ok(!relay.includes('colonyReclaimerGrantGrowth'));
});
