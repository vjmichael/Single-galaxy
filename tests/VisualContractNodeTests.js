'use strict';
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const test = require('node:test');
const {decodeFrames,applyFrames} = require('../viewer/contract.js');
const read = name=>fs.readFileSync(path.join(__dirname,'../recordings',name+'.sgview'),'utf8');
const run = name=>applyFrames(decodeFrames(read(name)));

test('10 asteroids despawn in viewer, ore conserved, hold 100',()=>{
  const frames=run('ten');assert.equal(frames.length,11);
  assert.equal([...frames[0].entities.values()].filter(e=>e.entityType===1).length,10);
  const final=frames.at(-1);const ship=[...final.entities.values()].find(e=>e.entityType===0);
  assert.equal(ship.cargo,100);assert.equal(ship.capacity,100);
  assert.equal([...final.entities.values()].filter(e=>e.entityType===1).length,0);
  assert.equal(frames.slice(1).flatMap(f=>f.notifications).filter(c=>c.type==='A'&&c.kind===0).length,10);
});
test('automatic mining stops at hold full, remaining rock 10',()=>{
  const frames=run('hold');assert.equal(frames.length,8);
  assert.deepEqual(frames.slice(1,4).map(f=>[...f.entities.values()].find(e=>e.entityType===0).cargo),[4,8,10]);
  const final=frames.at(-1);assert.equal(final.mining.size,0);
  const rock=[...final.entities.values()].find(e=>e.entityType===1);assert.equal(rock.ore,10);
  const stops=frames.flatMap(f=>f.notifications).filter(c=>c.type==='A'&&c.kind===2&&c.result===7);
  assert.equal(stops.length,1);
});
test('motion updates ship positions and preserves inertia',()=>{
  const frames=run('motion');const ship=f=>[...f.entities.values()].find(e=>e.entityType===0);
  assert.equal(frames.length,13);
  assert.ok(ship(frames.at(-1)).x>ship(frames[0]).x);
  assert.ok(ship(frames.at(-1)).vx>0);
  assert.equal(ship(frames.at(-1)).vx,ship(frames.at(-2)).vx);
});
test('packet corruption fails consistency hash',()=>{
  const tampered=read('hold').replace('E|3|1|1|0|2000|1000|0|0|20|0|0','E|3|1|1|0|2000|1000|0|0|19|0|0');
  assert.notEqual(tampered,read('hold'));
  assert.throws(()=>applyFrames(decodeFrames(tampered)),/hash mismatch/);
});
test('dropped delta does not silently desynchronize',()=>{
  const frames=decodeFrames(read('ten'));
  frames.splice(4,1);
  assert.throws(()=>applyFrames(frames),/Missing or out-of-order/);
});
test('noncanonical or invalid recording is rejected',()=>{
  assert.throws(()=>decodeFrames(read('hold').replace('SGVIEW|1','SGVIEW|2')),/Not an SGVIEW/);
  assert.throws(()=>decodeFrames(read('hold').replaceAll('\n','\r\n')),/canonical SGVIEW/);
  assert.throws(()=>decodeFrames(read('hold').slice(0,-4)),/canonical SGVIEW|Missing END/);
});
