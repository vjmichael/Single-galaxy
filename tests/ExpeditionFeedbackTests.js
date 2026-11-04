const test=require('node:test'),assert=require('node:assert/strict');
const {diagnostic,target,initial,advance,status}=require('../viewer/expedition-feedback.js');
const obs=(tick,hold,bay,docked=false,home=9)=>({tick,hold,bay,docked,home});
test('never declares success from a queued action, reconnect or existing cargo',()=>{
 let p=initial();assert.equal(p.success,false);
 p=advance(p,obs(1,10,40));assert.equal(p.mined,0);
 p=advance(p,obs(2,10,40));assert.equal(p.success,false);
 p=advance(p,obs(3,10,40,true));assert.equal(p.success,false);
 p=advance(p,obs(4,0,50,true));assert.equal(p.success,false);
 assert.match(status(p),/Docked/);
});
test('observed mine, dock and real increased home bay completes one cargo expedition',()=>{
 let p=advance(initial(),obs(1,0,0));
 p=advance(p,obs(2,8,0));assert.equal(p.mined,8);assert.equal(p.phase,'return');
 p=advance(p,obs(3,8,0,true));assert.equal(p.phase,'unload');
 p=advance(p,obs(4,0,8,true));assert.equal(p.success,true);assert.equal(p.delivered,8);
 assert.match(status(p),/Expedition complete/);
 // Repeated frames do not double count.
 p=advance(p,obs(4,0,8,true));assert.equal(p.delivered,8);
});
test('never credits old bay deposits, reloads or a foreign home',()=>{
 let p=advance(initial(),obs(10,3,7,true));
 p=advance(p,obs(11,4,6,true));assert.equal(p.mined,0);
 p=advance(p,obs(12,9,6,false));assert.equal(p.mined,5);
 p=advance(p,obs(13,9,6,true));
 p=advance(p,obs(14,4,12,true));assert.equal(p.delivered,5);
 p=advance(p,obs(1,4,12,true));assert.equal(p.success,false);
 p=advance(p,obs(2,4,12,true,10));assert.equal(p.success,false);
});
test('target display uses validated visible asteroid, not unobserved hidden resources',()=>{
 const ship={x:0,y:0,system:1};
 const rocks=[{id:2,entityType:1,system:1,x:3000,y:4000,ore:3},
 {id:3,entityType:1,system:1,x:1000,y:0,ore:0},
 {id:4,entityType:1,system:2,x:10,y:0,ore:10}];
 assert.equal(target(ship,rocks).id,2);
 assert.equal(target(ship,rocks,4).id,2);
 assert.equal(target(ship,rocks,2).distance,5000);
 assert.equal(target(ship,[]),null);
});
test('native rejection is explained but never marked committed from UI command receipt',()=>{
 assert.match(diagnostic('OUT_OF_RANGE',0),/Pilot closer/);
 assert.match(diagnostic('SHIP_MOVING',0),/velocity/);
 assert.match(diagnostic('APPLIED',8),/reported transfer 8/);
});
