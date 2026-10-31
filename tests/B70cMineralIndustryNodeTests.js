'use strict';
const test=require('node:test'), assert=require('node:assert/strict');
const {decode}=require('../viewer/colony.js');
test('B7.0c separated mineral and goods telemetry is bounded and explicitly typed',()=>{
 const clear=decode('COLONY|1|0|STOCK_CLEAR');assert.equal(clear.type,'STOCK_CLEAR');
 const mineral=decode('COLONY|1|21|STOCK|MINERAL|nickel_iron|10320');
 assert.deepEqual([mineral.category,mineral.name,mineral.kg],['MINERAL','nickel_iron',10320]);
 const goods=decode('COLONY|1|22|STOCK|GOOD|aluminum|190');
 assert.deepEqual([goods.category,goods.name,goods.kg],['GOOD','aluminum',190]);
 const tailings=decode('COLONY|1|22|STOCK|TAILINGS|1032');
 assert.equal(tailings.kg,1032);
 for(const raw of [
  'COLONY|1|22|STOCK|MINERAL|not_a_mineral|99',
  'COLONY|1|22|STOCK|GOOD|aluminum|0',
  'COLONY|1|22|STOCK|GOOD|evil<script>|123',
  'COLONY|1|22|STOCK|TAILINGS|-1',
  'COLONY|1|22|STOCK|MINERAL|plagioclase|999999999999999999',
  'COLONY|1|22|STOCK|MINERAL|nickel_iron|100|garbage',
  'COLONY|1|22|STOCK_CLEAR|extra',
 ]) assert.throws(()=>decode(raw),Error,raw);
});
