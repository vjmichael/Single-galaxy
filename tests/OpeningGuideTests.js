const assert=require('node:assert/strict');
const test=require('node:test');
const {progress}=require('../viewer/opening-guide.js');
const state=(raw,refined,modules,outposts)=>({crew:24,raw,refined,modules,outposts});
test('never invents progress without committed native STATUS',()=>{
 const p=progress(null,true);assert.equal(p.stage,-1);assert.equal(p.action,null);
 assert.equal(progress({crew:24,raw:-1,refined:0,modules:0,outposts:0}).stage,-1);
});
test('one physical opening path',()=>{
 assert.equal(progress(state(0,0,0,0),false).action,'colonyScan');
 assert.equal(progress(state(0,0,0,0),true).action,'colonyMine');
 assert.equal(progress(state(8,0,0,0),true).action,'colonyRefine');
 assert.equal(progress(state(0,8,0,0),true).action,'colonyFabricate');
 assert.equal(progress(state(0,0,4,0),true).action,'colonyBuild');
 const end=progress(state(0,0,0,1),false);assert.equal(end.done,true);assert.equal(end.action,null);
});
test('fresh replay and contradictory client hints never override world outpost',()=>{
 assert.equal(progress(state(0,0,0,1),false).stage,5);
 assert.equal(progress(state(0,0,0,0),false).stage,0);
 assert.equal(progress(state(0,0,4,0),false).stage,4);
});
