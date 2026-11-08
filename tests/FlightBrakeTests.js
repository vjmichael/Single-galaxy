const test=require('node:test'),assert=require('node:assert/strict');
const {plan}=require('../viewer/flight-brake.js');
const ship=(vx,vy,owner=7)=>({id:1,entityType:0,owner,vx,vy,system:1});
test('never auto-commands a stationary ship',()=>{
 assert.equal(plan(ship(0,0),{dockState:0}).status,'stopped');
});
test('brake uses genuine native opposing thrust axes, not velocity mutation',()=>{
 const original=ship(750,-500);
 assert.deepEqual(plan(original,{dockState:0}).command,{action:'thrust',extra:{x:-1,y:1}});
 assert.deepEqual(original,ship(750,-500));
 assert.deepEqual(plan(ship(-250,0),{dockState:0}).command.extra,{x:1,y:0});
});
test('rejects masked or missing hulls, docked ships, and transitions',()=>{
 assert.equal(plan(ship(250,0,0),{dockState:0}).status,'invalid');
 assert.equal(plan(null,{dockState:0}).status,'invalid');
 assert.equal(plan(ship(250,0),{dockState:1}).status,'invalid');
 assert.equal(plan(ship(250,0),{dockState:0,inTransition:true}).status,'invalid');
 assert.equal(plan(ship(NaN,0),{dockState:0}).status,'invalid');
});
