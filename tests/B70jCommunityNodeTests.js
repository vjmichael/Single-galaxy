'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {decode}=require('../viewer/colony.js');
const packet=s=>`COLONY|1|200|${s}`;
test('B7.0j community telemetry strict, bounded and nonclinical',()=>{
  const x=decode(packet('COMMUNITY|7|2|45|56|6|3|1|24'));
  assert.deepEqual([x.id,x.attendants,x.food,x.water,x.cycles,x.continuity,x.shortages,x.visits],
    [7,2,45,56,6,3,1,24]);
  for(const bad of [
    'COMMUNITY|0|2|45|56|6|3|1|24','COMMUNITY|7|5|45|56|6|3|1|24',
    'COMMUNITY|7|2|45|56|6|7|1|24','COMMUNITY|7|2|45|56|6|3|7|24',
    'COMMUNITY|7|2|45|56|6|3|1|49','COMMUNITY|7|2|45|56|6|3|1',
    'COMMUNITY|7|2|45|56|6|3|1|24|EXTRA','COMMUNITY|7|2|45|56|6|3|1|-1',
    'COMMUNITY|7|2|45|56|6|3|1|24<script>'
  ])assert.throws(()=>decode(packet(bad)),bad);
});
test('B7.0j independent community assessment never grants growth',()=>{
  const supported=decode(packet('COMMUNITY_ASSESS|2|2|1'));
  assert.equal(supported.leisure,1);
  const pending=decode(packet('COMMUNITY_ASSESS|1|2|0'));
  assert.equal(pending.leisure,0);
  for(const bad of ['COMMUNITY_ASSESS|1|2|1','COMMUNITY_ASSESS|2|1|0',
    'COMMUNITY_ASSESS|0|0|1','COMMUNITY_ASSESS|1|2|2',
    'COMMUNITY_ASSESS|1|2','COMMUNITY_ASSESS|1|2|0|1'])
    assert.throws(()=>decode(packet(bad)),bad);
  const resilience=decode(packet('RESILIENCE|20|10|1|1|1|1|3|1|1|1|0|3840'));
  assert.equal(resilience.growth,0);
  assert.equal(resilience.blockers&2048,2048);
  assert.equal(resilience.operationsCandidate,1);
});
test('B7.0j browser controls and relay use allowlisted commands only',()=>{
  const root=path.join(__dirname,'..');
  const html=fs.readFileSync(path.join(root,'viewer/interactive.html'),'utf8');
  const js=fs.readFileSync(path.join(root,'viewer/colony.js'),'utf8');
  const relay=fs.readFileSync(path.join(root,'live/interactive_relay.py'),'utf8');
  const host=fs.readFileSync(path.join(root,'src/colony_live_host.cpp'),'utf8');
  for(const id of ['colonyCommunityOutpost','colonyCommunityBuild','colonyCommunityStaffCount',
    'colonyCommunityStaff','colonyCommunityResource','colonyCommunityAmount',
    'colonyCommunitySupply','communityTelemetry','communityReadiness'])
    assert.ok(html.includes(`id="${id}"`),id);
  for(const protocol of ['COMMUNITY_BUILD','COMMUNITY_STOCK','COMMUNITY_STAFF',
    'COMMUNITY_ASSESS','COMMUNITY|'])assert.ok(host.includes(protocol),protocol);
  for(const action of ['colonyCommunityBuild','colonyCommunityStock','colonyCommunityStaff']){
    assert.ok(relay.includes(action),action);assert.ok(js.includes(action),action);
  }
  assert.ok(!relay.includes('colonyCommunityGrantGrowth'));
  assert.ok(html.includes('not clinical')||html.includes('clinical healthcare'));
});
