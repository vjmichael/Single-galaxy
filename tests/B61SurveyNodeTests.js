'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict');
const api=require('../viewer/survey.js');
test('B6.1 survey parser accepts bounded server-origin directional and probe messages',()=>{
 const known=api.decode('SURVEY|1|0|KNOWN|101|0|30000|30000');
 assert.equal(known.kind,0);assert.match(api.label(known),/Mapped on entry/);
 const d=api.decode('SURVEY|1|10|D|4|3|45|13000');
 assert.equal(d.kind,3);assert.equal(d.bearing,45);assert.match(api.label(d),/Station/);
 const p=api.decode('SURVEY|1|21|P|104|8|-10000|2500|62');
 assert.equal(p.x,-10000);assert.match(api.label(p),/Wormhole/);
 const a=api.decode('SURVEY|1|29|ARCHIVE|104|8|-10000|2500|62|21|0');
 assert.equal(a.published,0);assert.equal(a.observed,21);
 assert.equal(api.decode('SURVEY|1|31|PUBLISH|2').count,2);
});
test('B6.1 rejects fabricated, noncanonical, malformed and out-of-range scan messages',()=>{
 for(const raw of [
  'SURVEY|2|10|D|1|3|45|1000','SURVEY|1|10|D|1|3|360|1000',
  'SURVEY|1|10|D|1|3|045|1000','SURVEY|1|10|D|1|3|20|-1',
  'SURVEY|1|10|ARCHIVE|104|8|0|0|88|20|2',
  'SURVEY|1|10|P|104|8|0|0|101','SURVEY|1|10|DEPLOY|7001|0|0\nE|2',
  'SURVEY|1|10|D|1|3|0|9007199254740992','SURVEY|1|10|HACK|5',
  'SURVEY|1|10|ERROR|ANYTHING','SURVEY|1|10|D|1|3|0'
 ])assert.throws(()=>api.decode(raw),undefined,raw);
});
