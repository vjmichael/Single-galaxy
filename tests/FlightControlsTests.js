const test=require('node:test');const assert=require('node:assert/strict');
const {flightInput,guideSkipped,setGuideSkipped,rockRange}=require('../viewer/flight-controls.js');
test('keyboard maps to existing native thrust, never commands from form inputs',()=>{
 assert.deepEqual(flightInput('ArrowLeft'),{action:'thrust',extra:{x:-1,y:0}});
 assert.deepEqual(flightInput('w'),{action:'thrust',extra:{x:0,y:-1}});
 assert.deepEqual(flightInput(' '),{action:'thrust',extra:{x:0,y:0}});
 for(const key of ['Escape','Enter','q'])assert.equal(flightInput(key),null);
 assert.equal(flightInput('d',true),null);
 assert.equal(flightInput('d',false,true),null);
 assert.equal(flightInput('d',false,false,true),null);
 assert.equal(flightInput('d',false,false,false,true),null);
});
test('skip only hides local guide and tolerates blocked storage',()=>{
 const mem=new Map();const storage={getItem:k=>mem.get(k),setItem:(k,v)=>mem.set(k,v),removeItem:k=>mem.delete(k)};
 assert.equal(guideSkipped(storage),false);
 setGuideSkipped(storage,true);assert.equal(guideSkipped(storage),true);
 setGuideSkipped(storage,false);assert.equal(guideSkipped(storage),false);
 const blocked={getItem(){throw Error('blocked')},setItem(){throw Error('blocked')},removeItem(){throw Error('blocked')}};
 assert.equal(guideSkipped(blocked),false);assert.doesNotThrow(()=>setGuideSkipped(blocked,true));
});
test('range display is advisory only, never authorizes mining',()=>{
 assert.equal(rockRange({x:0,y:0},{x:3000,y:4000}),5000);
 assert.equal(rockRange(null,{x:0,y:0}),null);
 assert.equal(rockRange({x:0,y:0},{x:Infinity,y:0}),null);
});
