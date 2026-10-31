'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const {decodeFrames,applyNext,hashProjection}=require('../viewer/contract.js');
const start={system:1,tick:0,entities:new Map([
 [1,{id:1,system:1,entityType:2,owner:0,x:0,y:0,vx:0,vy:0,ore:0,cargo:0,capacity:0}],
 [2,{id:2,system:1,entityType:0,owner:7,x:0,y:0,vx:0,vy:0,ore:0,cargo:0,capacity:20}],
 [3,{id:3,system:1,entityType:1,owner:0,x:2500,y:700,vx:0,vy:0,ore:40,cargo:0,capacity:0}]
]),mining:new Map(),docks:new Map([[2,{id:2,state:1,station:1,remaining:0}]]),hangars:new Map([['1:7',{station:1,owner:7,ore:0,capacity:100}]])};
const snapshotRows=[...start.entities.values()].map(e=>['E',e.id,e.system,e.entityType,e.owner,e.x,e.y,e.vx,e.vy,e.ore,e.cargo,e.capacity].join('|'))
  .concat(['Q|2|1|1|0','H|1|7|0|100']);
function wire(state,rows,kind=1,tick=0,seq=0,hash=hashProjection(state)){
 return `SGVIEW|1|${kind}|100|${seq}|${tick}|1|${hash}|${rows.length}\n${rows.join('\n')}\nEND\n`;
}
test('B5.2 docked snapshot has verified station, dock state and local hangar',()=>{
 const state=applyNext(null,decodeFrames(wire(start,snapshotRows))[0]);
 assert.equal(state.docks.get(2).state,1);
 assert.equal(state.hangars.get('1:7').ore,0);
});
test('B5.2 tampered dock state cannot pass projection hash',()=>{
 const bad=wire(start,snapshotRows).replace('Q|2|1|1|0','Q|2|2|1|2');
 assert.throws(()=>applyNext(null,decodeFrames(bad)[0]),/Frame projection hash mismatch/);
});
test('B5.2 invalid hangar amounts and dock station are rejected',()=>{
 let text=wire(start,snapshotRows).replace('H|1|7|0|100','H|1|7|101|100');
 assert.throws(()=>applyNext(null,decodeFrames(text)[0]),/Invalid hangar telemetry/);
 text=wire(start,snapshotRows).replace('Q|2|1|1|0','Q|2|1|999|0');
 assert.throws(()=>applyNext(null,decodeFrames(text)[0]),/Invalid docking telemetry/);
});
test('B5.2 undocking deletion and inventory delta apply only after correct hash',()=>{
 const prior=applyNext(null,decodeFrames(wire(start,snapshotRows))[0]);
 const next={...start,tick:1,docks:new Map(),hangars:new Map([['1:7',{station:1,owner:7,ore:20,capacity:100}]])};
 const changes=['Z|2','H|1|7|20|100','C|1|101|0|20'];
 const frame=decodeFrames(wire(next,changes,2,1,1))[0];
 const applied=applyNext(prior,frame);
 assert.equal(applied.docks.size,0);
 assert.equal(applied.hangars.get('1:7').ore,20);
 assert.equal(applied.notifications[0].transferred,20);
 assert.throws(()=>applyNext(prior,{...frame,sequence:2}),/Missing or out-of-order delta/);
});
