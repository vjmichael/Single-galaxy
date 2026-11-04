const test=require('node:test');
const assert=require('node:assert/strict');
const {create,modes}=require('../viewer/scanner-session.js');

test('host-observed signature lock and range-driven request',()=>{
 const s=create();
 assert.deepEqual(modes,['environmental','celestial','anomaly']);
 assert.throws(()=>s.lock(17),/undiscovered/);
 assert.throws(()=>s.refinementRequest(),/Lock/);
 s.acceptContacts([{id:17,coarseX:10000,coarseY:0,
   uncertaintyMm:5000,secretPosition:9827,kind:'hidden'}]);
 assert.deepEqual(s.state().contacts,[{id:17,coarseX:10000,
   coarseY:0,uncertaintyMm:5000}]);
 s.setRadius(8000);s.lock(17);
 assert.deepEqual(s.refinementRequest(),{action:'refineSignature',
   mode:'anomaly',signature:17,radiusMm:8000});
 s.acceptContacts([]);assert.equal(s.state().selected,null);
});
test('mode switching, ownership boundary and malformed reports',()=>{
 const s=create();
 assert.throws(()=>s.setRadius(-1),/radius/);
 assert.throws(()=>s.selectMode('supernatural'),/mode/);
 assert.throws(()=>s.acceptContacts([{id:0,coarseX:0,coarseY:0,uncertaintyMm:5}]),/signature/);
 assert.throws(()=>s.acceptContacts([{id:5,coarseX:0,coarseY:0,uncertaintyMm:5},
   {id:5,coarseX:0,coarseY:0,uncertaintyMm:5}]),/signature/);
 s.acceptContacts([{id:4,coarseX:0,coarseY:0,uncertaintyMm:5000}]);
 s.lock(4);s.selectMode('celestial');
 assert.equal(s.state().selected,null);
 assert.deepEqual(s.state().contacts,[]);
});
