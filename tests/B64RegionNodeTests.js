'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const api=require('../viewer/survey.js');
require('../viewer/region.js');
const region=globalThis.GalaxyRegionView;
test('B6.4 seven original systems have nine mapped links and named sovereignty zones',()=>{
 assert.equal(region.nodes.length,7);
 assert.equal(region.links.length,9);
 assert.equal(new Set(region.nodes.map(s=>s.id)).size,7);
 assert.ok(region.nodes.find(s=>s.id===7&&s.name==='Far Lantern'));
 assert.ok(region.links.some(x=>x[0]===1&&x[1]===3));
});
test('B6.4 accepts committed multi-hop paths and next-leg indication',()=>{
 const shortest=api.decode('SURVEY|1|10|ROUTE|7|S|300|1-2-6-7');
 const safer=api.decode('SURVEY|1|10|ROUTE|7|Q|800|1-3-4-5-7');
 const leg=api.decode('SURVEY|1|11|LEG|2|16');
 assert.deepEqual(shortest.path,[1,2,6,7]);
 assert.deepEqual(safer.path,[1,3,4,5,7]);
 assert.equal(leg.next,2);assert.equal(leg.gate,16);
 assert.match(api.label(leg),/Next leg/);
 assert.equal(api.decode('SURVEY|1|40|LEG|0|0').next,0);
 region.onEvent({type:'SYSTEM',system:1});
 region.onEvent(shortest);
 region.onEvent(leg);
 region.onEvent({type:'SYSTEM',system:2});
 region.onEvent({type:'LEG',next:6,gate:32});
});
test('B6.4 rejects malformed route-leg telemetry',()=>{
 for(const s of ['SURVEY|1|10|LEG|9|5','SURVEY|1|10|LEG|0|5',
                 'SURVEY|1|10|LEG|2|0','SURVEY|1|10|LEG|-1|3',
                 'SURVEY|1|10|LEG|1|1|1','SURVEY|1|10|LEG|01|4'])
  assert.throws(()=>api.decode(s),undefined,s);
});
test('B6.4 local interface exposes route selection and manual physical leg controls',()=>{
 const root=path.join(__dirname,'../viewer');
 const html=fs.readFileSync(path.join(root,'interactive.html'),'utf8');
 const controls=fs.readFileSync(path.join(root,'interactive.js'),'utf8');
 const relay=fs.readFileSync(path.join(__dirname,'../live/interactive_relay.py'),'utf8');
 for(let n=1;n<=7;n++) assert.match(html,new RegExp(`<option value="${n}">`));
 for(const id of ['regionMap','travelHop','jumpHop','routeDestination'])
   assert.ok(html.includes(`id="${id}"`),id);
 for(const action of ['travelHop','jumpHop']){
   assert.ok(controls.includes(`['${action}','${action}'`));
   assert.ok(relay.includes(`'${action}'`));
 }
});
