const test=require('node:test'),assert=require('node:assert/strict');
const {axis,plan,target,statusText}=require('../viewer/travel-assist.js');
const ship=(x=0,y=0,vx=0,vy=0)=>({x,y,vx,vy,system:1});
const site=(x=20000,y=0)=>({x,y,system:1});
test('acceleration uses existing one-step thrust bounds and brakes with native inertia',()=>{
 assert.deepEqual(plan(ship(),site()).command,{action:'thrust',extra:{x:1,y:0}});
 assert.deepEqual(plan(ship(19900,0,2000,0),site()).command,{action:'thrust',extra:{x:-1,y:0}});
 assert.deepEqual(plan(ship(20000,0,0,0),site()).status,'arrived');
 assert.deepEqual(plan(ship(20000,0,250,0),site()).command,{action:'thrust',extra:{x:-1,y:0}});
 for(const vx of [-20000,-1234,0,173,20000])
  for(const x of [0,18000,20000,25000]){
   const p=plan(ship(x,0,vx,0),site());
   if(p.status==='thrust'){
     assert.ok([-1,0,1].includes(p.command.extra.x));
     assert.ok([-1,0,1].includes(p.command.extra.y));
   }
  }
});
test('invalid, changing or in-transition world state refuses assistance',()=>{
 assert.equal(plan(null,site()).status,'invalid');
 assert.equal(plan(ship(),{...site(),system:2}).status,'invalid');
 assert.equal(plan(ship(),site(),{dockState:1}).status,'invalid');
 assert.equal(plan(ship(),site(),{jumpActive:true}).status,'invalid');
 assert.equal(plan(ship(),site(),{tolerance:4000}).status,'invalid');
});
test('only actual native visible target and verified owned constructed home can be chosen',()=>{
 const state={entities:new Map([[2,{id:2,entityType:1,ore:40,system:1,x:20000,y:0}],
  [3,{id:3,entityType:2,owner:7,system:1,x:1000,y:0}],
  [4,{id:4,entityType:2,owner:8,system:1,x:1000,y:0}]])};
 assert.equal(target(state,'rock',2,new Set([3])).id,2);
 assert.equal(target(state,'home',3,new Set([3])).id,3);
 assert.equal(target(state,'home',3,new Set()),null);
 assert.equal(target(state,'home',4,new Set([4])),null);
 assert.equal(target(state,'rock',999,new Set()),null);
});
test('cancel only disengages control, never claims to stop physical velocity',()=>{
 assert.match(statusText('cancelled','home'),/retains real momentum/);
});
