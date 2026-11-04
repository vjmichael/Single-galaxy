const test=require('node:test'),assert=require('node:assert/strict');
const {entries,describe,stats}=require('../viewer/exploration-journal.js');
const records=new Map([[3,{site:3,system:1,x:1000,y:0,firstTick:7,geological:true}],
 [2,{site:2,system:1,x:2000,y:0,firstTick:2,geological:false}],
 [4,{site:4,system:2,x:0,y:0,firstTick:19,geological:true}]]);
test('saved discoveries sort by first native observation with no invented data',()=>{
 const rows=entries(records,1,{x:0,y:0,system:1});
 assert.deepEqual(rows.map(r=>r.site),[2,3,4]);
 assert.equal(rows[0].distance,2000);assert.equal(rows[2].distance,null);
 assert.deepEqual(stats(rows),{total:3,local:2,geological:2,systems:2});
 assert.match(describe(rows[1]),/first discovered tick 7/);
});
test('invalid and absent discovery records are not reported as observations',()=>{
 assert.deepEqual(entries(null),[]);
 assert.deepEqual(entries(new Map([[1,{site:0,firstTick:1,system:1,x:0,y:0}]])),[]);
});
