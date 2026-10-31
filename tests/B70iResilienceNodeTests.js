'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {decode}=require('../viewer/colony.js');
const packet=s=>`COLONY|1|200|${s}`;
test('B7.0i strict resilience diagnostic and no autonomous-growth claim',()=>{
  const s=decode(packet('RESILIENCE|20|10|1|1|1|1|3|1|1|1|0|1792'));
  assert.equal(s.operationsCandidate,1);assert.equal(s.growth,0);
  assert.equal(s.blockers,1792);assert.equal(s.siteCycles,10);
  const large=decode(packet('RESILIENCE|100000|250000|1|1|1|1|3|1|1|1|0|1792'));
  assert.equal(large.onboardCycles,100000); // legitimate large physical stock
  const blocked=decode(packet('RESILIENCE|20|4|1|0|1|0|1|0|0|0|0|1828'));
  assert.equal(blocked.operationsCandidate,0);
  for(const bad of [
    'RESILIENCE|20|10|1|1|1|1|3|1|1|1|1|1792', // claims growth
    'RESILIENCE|20|10|1|1|1|1|3|1|1|0|0|1792', // inconsistent flag
    'RESILIENCE|20|10|1|2|1|1|3|1|1|1|0|1792', // impossible coverage
    'RESILIENCE|20|10|1|1|1|1|3|1|1|1|0|0',    // falsely verified services
    'RESILIENCE|20|10|1|1|1|1|3|1|1|1|0|1792|extra',
    'RESILIENCE|20|10|1|1|1|1|3|1|1|1|0|-1',
    'RESILIENCE|20|10|1|1|1|1|3|1|1|1|0|1792<script>'
  ])assert.throws(()=>decode(packet(bad)),bad);
});
test('B7.0i telemetry is read-only and visible in local interface',()=>{
  const root=path.join(__dirname,'..');
  const html=fs.readFileSync(path.join(root,'viewer/interactive.html'),'utf8');
  const js=fs.readFileSync(path.join(root,'viewer/colony.js'),'utf8');
  const host=fs.readFileSync(path.join(root,'src/colony_live_host.cpp'),'utf8');
  assert.ok(html.includes('id="colonyResilience"'));
  assert.ok(html.includes('Autonomous')||html.includes('growth'));
  assert.ok(js.includes("e.type==='RESILIENCE'"));
  assert.ok(js.includes('textContent'));
  assert.ok(host.includes('assess_resilience(ship)'));
  assert.ok(host.includes('RESILIENCE|'));
  const relay=fs.readFileSync(path.join(root,'live/interactive_relay.py'),'utf8');
  assert.ok(!relay.includes('colonyResilienceAuthorize')); // no privilege path
});
