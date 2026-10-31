'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {decode}=require('../viewer/colony.js');
const packet=x=>`COLONY|1|200|${x}`;
test('B7.0l local purified-water telemetry is mass-balanced',()=>{
 const p=decode(packet('WATERWORKS|7|940|6|54|5|3'));
 assert.deepEqual([p.id,p.raw,p.brine,p.potable,p.cycles,p.continuity],[7,940,6,54,5,3]);
 for(const bad of [
  'WATERWORKS|0|940|6|54|5|3',
  'WATERWORKS|7|940|6|55|5|3',
  'WATERWORKS|7|940|6|54|5|6',
  'WATERWORKS|7|-940|6|54|5|3',
  'WATERWORKS|7|940|6|54|5',
  'WATERWORKS|7|940|6|54|5|3|1'
 ])assert.throws(()=>decode(packet(bad)),bad);
});
test('B7.0l three-period water supply cannot claim infinite renewal',()=>{
 const a=decode(packet('WATER_SUPPLY|2|2|2|108|24|1|0'));
 assert.equal(a.covered,1);assert.equal(a.renewable,0);
 for(const bad of [
  'WATER_SUPPLY|1|2|1|54|12|1|0',
  'WATER_SUPPLY|2|2|1|54|12|1|0',
  'WATER_SUPPLY|2|2|2|12|54|1|0',
  'WATER_SUPPLY|2|2|2|54|12|1|1',
  'WATER_SUPPLY|2|2|2|54|12|2|0'
 ])assert.throws(()=>decode(packet(bad)),bad);
});
test('B7.0l UI and relay contain bounded local player commands only',()=>{
 const root=path.join(__dirname,'..');
 const html=fs.readFileSync(path.join(root,'viewer/interactive.html'),'utf8');
 const ui=fs.readFileSync(path.join(root,'viewer/colony.js'),'utf8');
 const relay=fs.readFileSync(path.join(root,'live/interactive_relay.py'),'utf8');
 const host=fs.readFileSync(path.join(root,'src/colony_live_host.cpp'),'utf8');
 for(const id of ['colonyWaterworksOutpost','colonyWaterworksBuild','colonyWaterworksAmount','colonyWaterworksFeed','waterworksReadiness','waterworksTelemetry'])
  assert.ok(html.includes(`id="${id}"`),id);
 assert.ok(ui.includes('colonyWaterworksFeed'));
 assert.ok(host.includes('WATERWORKS_FEED')&&host.includes('WATER_SUPPLY|'));
 assert.ok(relay.includes('n%1000')&&relay.includes('colonyWaterworksBuild'));
 assert.ok(!relay.includes('colonyGrantAutonomousGrowth'));
});
