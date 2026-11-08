'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const api=require('../viewer/first-person.js');
const e=(id,kind,owner,x,y,system=37,ore=0,cargo=0)=>
 ({id,entityType:kind,owner,x,y,system,ore,cargo});
const frame=(entities,system=37)=>({tick:55,system,entities:new Map(entities.map(x=>[x.id,x]))});
const options={activeId:1,homeId:1,heading:90,headingShipId:1};
test('first-person viewpoint is a real native owned ship, never an anonymous contact',()=>{
 const home=e(1,0,7,1000000,3000000),unknown=e(3,0,0,1000100,3000100);
 assert.equal(api.project(frame([unknown]),{...options,activeId:3,homeId:3}).status,'no-owned-ship');
 assert.equal(api.project(frame([home,unknown]),{...options,activeId:3}).status,'no-owned-ship');
 assert.equal(api.project(frame([home,unknown]),options).shipId,1);
});
test('camera yaw must come from the same verified selected ship',()=>{
 const f=frame([e(1,0,7,0,0)]);
 assert.equal(api.project(f,{...options,headingShipId:2}).status,'no-native-heading');
 assert.equal(api.project(f,{...options,heading:360}).status,'no-native-heading');
 assert.equal(api.project(f,options).headingDegrees,90);
});
test('3D primitives are system-local and based on authoritative relative XY',()=>{
 const f=frame([e(1,0,7,1000000,3000000),e(2,1,0,1001000,3002000,37,100),
  e(4,2,7,1003000,2999000,37),e(5,0,0,1010000,3000000),
  e(6,2,7,1000500,3000500,1),e(7,1,0,4000000,3000000,37,100)]);
 const result=api.project(f,options);
 assert.deepEqual(result.objects.map(o=>o.id),[2,4,5]);
 assert.deepEqual(result.objects[0].x,1);
 assert.deepEqual(result.objects[0].z,2);
 assert.equal(result.objects[1].kind,2);
 assert.equal(result.objects[2].kind,0);
});
test('anonymous hull cargo must stay private in 3D and missing source rejects',()=>{
 const a=e(1,0,7,0,0),foreign=e(2,0,0,1000,1000,37,0,9);
 assert.deepEqual(api.project(frame([a,foreign]),options).objects,[]);
 assert.equal(api.project(null,options).status,'no-frame');
});
test('viewer integration retains 2D fallback and does not add an unreviewed movement authority',()=>{
 const dir=path.resolve(__dirname,'../viewer');
 const html=fs.readFileSync(path.join(dir,'interactive.html'),'utf8');
 const viewer=fs.readFileSync(path.join(dir,'viewer.js'),'utf8');
 const first=fs.readFileSync(path.join(dir,'first-person.js'),'utf8');
 for(const id of ['fpsSpace','pilotView3D','pilotView2D','pilotViewControls','pilotViewStatus'])
   assert.match(html,new RegExp('id="'+id+'"'));
 assert.match(html,/first-person\.js/);
 assert.match(viewer,/GalaxyFirstPersonUI\?\.update\(state\)/);
 assert.match(viewer,/GalaxyFirstPerson\?\.init\(\)/);
 assert.doesNotMatch(first,/fetch\s*\(|\/command|WebSocket|EventSource/);
 assert.match(first,/getContext\('webgl2'/);
});
