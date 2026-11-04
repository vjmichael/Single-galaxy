const test=require('node:test'),assert=require('node:assert/strict');
const {plan}=require('../viewer/cargo-return.js');
function state({shipX=0,shipY=0,vx=0,cargo=10,baseX=1000,dockState=0,bayOre=0}={}){
 const entities=new Map([[1,{id:1,entityType:0,owner:7,system:1,x:shipX,y:shipY,vx,vy:0,cargo,capacity:60}],
   [5,{id:5,entityType:2,owner:7,system:1,x:baseX,y:0}]]);
 return {entities,docks:new Map([[1,{state:dockState,station:dockState===1?5:0}]]),
   hangars:new Map([['5:7',{ore:bayOre}]])};
}
test('no fake cargo or home base before projection',()=>{
 assert.equal(plan(null).ready,false);
 const s=state();s.entities.delete(5);
 assert.equal(plan(s).canDock,false);
 assert.equal(plan(s).site,null);
});
test('navigation remains advisory while real docking checks speed and range',()=>{
 assert.equal(plan(state({shipX:4000})).canDock,false);
 assert.equal(plan(state({vx:50})).canDock,false);
 assert.equal(plan(state()).canDock,true);
 assert.equal(plan(state()).distance,1000);
});
test('only actual docking and hangar inventory enable physical transfers',()=>{
 const s=state({dockState:1,bayOre:4});
 assert.equal(plan(s).canUnload,true);
 assert.equal(plan(s).canLoad,true);
 assert.equal(plan(s).canUndock,true);
 assert.equal(plan(state({dockState:0,bayOre:4})).canUnload,false);
 assert.equal(plan(state({dockState:1,cargo:0})).canUnload,false);
 assert.equal(plan(state({dockState:1,bayOre:0})).canLoad,false);
});
test('reject foreign outposts as home and keep real source inventory distinct',()=>{
 const s=state();s.entities.get(5).owner=8;
 assert.equal(plan(s).site,null);
});
