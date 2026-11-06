'use strict';
// SHIP-STATUS-05: native-telemetry contract for the actual player cockpit.
// Node-only; no DOM, game server, network, player credentials or fake results.
const assert=require('node:assert/strict');
const cyber=require('../viewer/cyber-controls.js');
const status=cyber.parse('COLONY|1|5|CYBER_STATUS|17|1|0|3|1');
assert.equal(status.ship,17);
assert.equal(status.emitter,true);
assert.equal(status.firewall,false);
assert.equal(status.cooldown,3);
assert.equal(status.colony,true);
// Fitted is not enough: a real emitter has to be effective and non-isolated
// on the current committed tick before the cockpit can request an attack.
assert.equal(cyber.canAttack(status,22,true),false);
assert.equal(cyber.canAttack({...status,cooldown:0},22,true),true);
assert.equal(cyber.canAttack({...status,cooldown:0},22,false),false);
assert.equal(cyber.canAttack({...status,cooldown:0},22),false);
assert.equal(cyber.canAttack({...status,emitter:false,cooldown:0},22,true),false);
assert.equal(cyber.canAttack({...status,cooldown:0},0,true),false);
assert.equal(cyber.canAttack({...status,cooldown:0},22,1),false);
const moduleState=cyber.parse('COLONY|1|5|CYBER_MODULE|17|0|1|1|0|0');
assert.equal(moduleState.hardware,1);
assert.equal(moduleState.requested,true);
assert.equal(moduleState.effective,false);
assert.equal(moduleState.isolated,false);
assert.equal(cyber.parse('COLONY|1|5|CYBER_MODULE|17|1|0|1|0|0').hardware,0);
assert.equal(cyber.parse('COLONY|1|5|CYBER_TARGETS_RESET|17').ship,17);
assert.equal(cyber.parse('COLONY|1|5|CYBER_TARGET|17|22|7000').distance,7000);
assert.equal(cyber.parse('COLONY|1|6|CYBER_RESULT|991|3|ATTACK_RESISTED').result,'ATTACK_RESISTED');
for(const forged of [
 'COLONY|1|x|CYBER_STATUS|17|1|0|0|1',
 'COLONY|1|5|CYBER_STATUS|17|2|0|0|1',
 'COLONY|1|5|CYBER_MODULE|17|2|1|1|0|0',
 'COLONY|1|5|CYBER_MODULE|17|0|0|1|1|0',
 'COLONY|1|5|CYBER_TARGET|17|17|5',
 'COLONY|1|5|CYBER_TARGET|17|22|9000',
 'COLONY|1|5|CYBER_RESULT|0|3|APPLIED',
 'COLONY|1|5|CYBER_RESULT|991|3|<script>',
 'COLONY|1|5|CYBER_STATUS|17|1|0|0|1|extra'
 ])assert.throws(()=>cyber.parse(forged),undefined,forged);
console.log('PASS SHIP-STATUS-05: strictly parsed committed cyber cockpit telemetry, effective state, and target gates');
