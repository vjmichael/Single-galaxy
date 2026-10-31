'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict');
const api=require('../viewer/survey.js');
test('B6.3 validates system routes and labels advisory vs travel',()=>{
 const r=api.decode('SURVEY|1|20|ROUTE|2|Q|380|1-2');
 assert.equal(r.dest,2);assert.deepEqual(r.path,[1,2]);
 assert.match(api.label(r),/safer/);
 assert.equal(api.decode('SURVEY|1|30|ROUTE_NONE|2|S').dest,2);
});
test('B6.3 validates history timestamps and publication flags',()=>{
 const h=api.decode('SURVEY|1|53|HISTORY|103|6|10000|-2500|65|9|0');
 assert.equal(h.observed,9);assert.equal(h.published,0);
 assert.match(api.label(h),/historical/);
 assert.equal(api.decode('SURVEY|1|55|HISTORY_DONE|3').count,3);
});
test('B6.3 rejects fabricated routes, malformed history and noncanonical data',()=>{
 for (const data of [
  'SURVEY|1|20|ROUTE|2|S|100|1-3', // wrong destination
  'SURVEY|1|20|ROUTE|2|S|100|1-1-2', // cycle
  'SURVEY|1|20|ROUTE|2|F|100|1-2', // unsupported policy
  'SURVEY|1|20|ROUTE|2|S|-100|1-2',
  'SURVEY|1|20|ROUTE|2|S|0100|1-2',
  'SURVEY|1|20|ROUTE_NONE|2|BAD',
  'SURVEY|1|20|HISTORY|103|6|0|0|66|10|2',
  'SURVEY|1|20|HISTORY|103|6|0|0|101|10|1',
  'SURVEY|1|20|HISTORY|103|6|0|0|50|10'
 ])assert.throws(()=>api.decode(data),undefined,data);
});
