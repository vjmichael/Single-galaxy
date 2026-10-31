'use strict';
const test=require('node:test'), assert=require('node:assert/strict');
const {interpolatePositions}=require('../viewer/interpolation.js');
const contract=require('../viewer/contract.js');
const f=require('node:fs').readFileSync(require('node:path').join(__dirname,'../motion.live.sgview'),'utf8');
const states=contract.applyFrames(contract.decodeFrames(f));
test('interpolation only changes presentation coordinates, not authoritative state',()=>{
 const prev=states[1],now=states[2],id=[...now.entities.keys()].find(k=>now.entities.get(k).entityType===0);
 const start=prev.entities.get(id),end=now.entities.get(id);
 const a=interpolatePositions(prev,now,0),b=interpolatePositions(prev,now,0.5),c=interpolatePositions(prev,now,1);
 assert.equal(a.entities.get(id).x,start.x);assert.equal(c.entities.get(id).x,end.x);
 assert.equal(b.entities.get(id).x,(start.x+end.x)/2);
 assert.equal(b.entities.get(id).y,(start.y+end.y)/2);
 assert.equal(now.entities.get(id).x,end.x);
 assert.equal(contract.hashProjection(now),contract.decodeFrames(f)[2].hash); // no mutated C++-validated state
});
test('clamps progress and preserves entity removals, regardless of prior visibility',()=>{
 const prev={stream:'1',system:1,entities:new Map([[2,{id:2,x:10,y:20}],[3,{id:3,x:0,y:0}]])};
 const now={stream:'1',system:1,entities:new Map([[2,{id:2,x:30,y:40}],[4,{id:4,x:123,y:456}]])};
 const state=interpolatePositions(prev,now,.5);
 assert.equal(state.entities.has(3),false);assert.equal(state.entities.get(4).x,123);
 assert.equal(state.entities.get(2).x,20);
 assert.equal(interpolatePositions(prev,now,-2).entities.get(2).x,10);
 assert.equal(interpolatePositions(prev,now,100).entities.get(2).x,30);
});
test('incremental frame validator matches original full-recording validator',()=>{
 const frames=contract.decodeFrames(f),original=contract.applyFrames(frames);
 let current=null;
 for(let i=0;i<frames.length;i++){
  current=contract.applyNext(current,frames[i]);
  assert.equal(contract.hashProjection(current),contract.hashProjection(original[i]));
  assert.equal(current.tick,original[i].tick);
 }
});
