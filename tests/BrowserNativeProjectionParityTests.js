'use strict';
const assert=require('node:assert/strict');
const test=require('node:test');
const {applyNext,decodeFrames,hashProjection}=require('../viewer/contract.js');
const empty=()=>({system:1,tick:0,entities:new Map(),mining:new Map(),docks:new Map(),
  jumps:new Map(),hangars:new Map(),wallets:new Map(),markets:new Map()});
const entity=(id,entityType,owner,cargo=0,capacity=0,ore=0)=>({
  type:'E',id,system:1,entityType,owner,x:100*id,y:0,vx:0,vy:0,ore,cargo,capacity
});
function run(entities){
  const state=empty();
  for(const e of entities)state.entities.set(e.id,e);
  const hash=hashProjection(state);
  const header=['SGVIEW','1','1','99','0','0','1',hash,String(entities.length)].join('|');
  const lines=[header,
    ...entities.map(e=>['E',e.id,e.system,e.entityType,e.owner,e.x,e.y,e.vx,e.vy,e.ore,e.cargo,e.capacity].join('|')),
    'END',''];
  return applyNext(null,decodeFrames(lines.join('\n'))[0]);
}
test('native masked foreign owner=0 accepts only sanitized cargo',()=>{
  const own=entity(1,0,7,0,60);
  const anon=entity(2,0,0,0,75);
  const s=run([own,anon]);
  assert.equal(s.entities.get(2).owner,0);
  assert.equal(s.entities.get(1).owner,7);
  assert.throws(()=>run([own,entity(2,0,0,1,75)]),/Invalid ship/);
});
test('native wreck kind 4 validates finite salvage and owner',()=>{
  const own=entity(1,0,7,0,60);
  assert.equal(run([own,entity(3,4,7,8,60)]).entities.get(3).cargo,8);
  assert.throws(()=>run([own,entity(3,4,0,8,60)]),/Invalid wreck/);
  assert.throws(()=>run([own,entity(3,4,7,61,60)]),/Invalid wreck/);
});
test('invalid own hull still rejected; normal colony snapshot passes',()=>{
  const own=entity(1,0,7,0,60);
  assert.throws(()=>run([entity(1,0,7,61,60)]),/Invalid ship/);
  assert.throws(()=>run([entity(1,0,7,0,0)]),/Invalid ship/);
  assert.deepEqual([...run([own,entity(2,1,0,0,0,40)]).entities.keys()],[1,2]);
});
