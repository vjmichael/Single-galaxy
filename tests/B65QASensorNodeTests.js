'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path');
const sensor=require('../viewer/survey.js');
require('../viewer/region.js');
test('B6.5 accepts explicit QA gate status and authoritative reroutes',()=>{
 const outage=sensor.decode('SURVEY|1|16|QA_GATE|2|6|0');
 const reroute=sensor.decode('SURVEY|1|17|REROUTE|7|S|700|2-4-5-7');
 assert.deepEqual([outage.from,outage.to,outage.online],[2,6,false]);
 assert.deepEqual(reroute.path,[2,4,5,7]);
 assert.match(sensor.label(reroute),/Route revised/);
 globalThis.GalaxyRegionView.onEvent({type:'SYSTEM',system:2});
 globalThis.GalaxyRegionView.onEvent(reroute);
 assert.equal(sensor.decode('SURVEY|1|19|QA_DONE|7').value,7);
 assert.equal(sensor.decode('SURVEY|1|20|QA_MODE|1').value,1);
});
test('B6.5 rejects malformed or forged QA telemetry',()=>{
 for(const raw of ['SURVEY|1|10|QA_GATE|2|6|2','SURVEY|1|10|QA_GATE|2|2|0',
  'SURVEY|1|10|QA_GATE|02|6|0','SURVEY|1|10|QA_GATE|2|6',
  'SURVEY|1|10|REROUTE|7|S|30|2-2-7','SURVEY|1|10|QA_DONE|-1',
  'SURVEY|1|10|QA_MODE|999'])assert.throws(()=>sensor.decode(raw),undefined,raw);
});
test('B6.5 only exposes test automation controls behind server QA mode',()=>{
 const html=fs.readFileSync(path.join(__dirname,'../viewer/interactive.html'),'utf8');
 const client=fs.readFileSync(path.join(__dirname,'../viewer/interactive.js'),'utf8');
 const relay=fs.readFileSync(path.join(__dirname,'../live/interactive_relay.py'),'utf8');
 for(const id of ['qaPanel','qaRoute','qaStop','qaGateDown','qaGateUp'])assert.ok(html.includes(`id="${id}"`),id);
 assert.match(html,/id="qaPanel" hidden/);
 assert.match(client,/qaPanel'\)\.hidden=!data\.qa_mode/);
 assert.match(relay,/--qa-test/);
 assert.match(relay,/QA testing mode disabled/);
});
