'use strict';
const {test}=require('node:test'),assert=require('node:assert/strict');
const survey=require('../viewer/survey.js');
test('B6.6 local gate health status is decoded and displayed',()=>{
  const record=survey.decode('SURVEY|1|10|GATE_STATUS|2|6|20|995|2|199|1');
  assert.deepEqual([record.from,record.to,record.id,record.condition,record.technicians,record.parts,record.online],
    [2,6,20,995,2,199,true]);
  assert.match(survey.label(record),/condition 995\/1000/);
  assert.equal(survey.decode('SURVEY|1|12|GATE_STATUS|2|6|20|0|0|0|0').online,false);
});
test('B6.6 rejects malformed, out-of-range and impossible gate status',()=>{
  for(const raw of [
    'SURVEY|1|10|GATE_STATUS|2|6|20|0|2|199|1',
    'SURVEY|1|10|GATE_STATUS|2|6|20|1001|2|199|1',
    'SURVEY|1|10|GATE_STATUS|2|6|20|900|17|199|1',
    'SURVEY|1|10|GATE_STATUS|2|6|20|900|2|1000001|1',
    'SURVEY|1|10|GATE_STATUS|2|2|20|900|2|199|1',
    'SURVEY|1|10|GATE_STATUS|2|6|20|900|2|199',
    'SURVEY|1|10|GATE_STATUS|02|6|20|900|2|199|1',
    'SURVEY|1|10|GATE_STATUS|2|6|20|-1|2|199|1'
  ])assert.throws(()=>survey.decode(raw),undefined,raw);
});
