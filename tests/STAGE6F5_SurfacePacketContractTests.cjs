'use strict';
// S6-F5: strict native person/surface packet decoder, NOT a client world.
const assert=require('node:assert/strict');
const {decode}=require('../viewer/colony.js');
const row=(type,...fields)=>'COLONY|1|30|'+type+'|'+fields.join('|');
const rejected=value=>assert.throws(()=>decode(value));
const admission=decode(row('PLANET_PLAYER_REGISTERED',7,100,19));
assert.equal(admission.owner,7);
assert.equal(admission.ship,100);
const accepted=decode(row('PLANET_PLAYER_COMMITTED',7,200,17,36,'DISEMBARK',81102,20));
assert.equal(accepted.operation,'DISEMBARK');
assert.equal(accepted.receipt,81102);
const custody=decode(row('PLANET_PLAYER_STATUS',
    7,100,7,200,17,36,1,81234,81234,0,81102,30,21));
assert.equal(custody.state,7);
assert.equal(custody.body,200);
assert.equal(custody.generation,21);
const commit=decode(row('PLANET_SURFACE_COMMITTED',0,82001,22));
assert.equal(commit.receipt,82001);
const original=decode(row('PLANET_SURFACE_POSE',
    7,0,200,17,36,300,-12000,0,30,82001,30,22));
assert.equal(original.x,300);
assert.equal(original.y,-12000);
assert.equal(original.z,0);
assert.equal(original.lastMoveTick,30);
assert.equal(original.receipt,82001);
assert.equal(original.owner,7);
assert.equal(decode(row('ERROR','PLANET_SURFACE_DENIED')).error,
    'PLANET_SURFACE_DENIED');
assert.equal(decode(row('ERROR','PLANET_PLAYER_DENIED')).error,
    'PLANET_PLAYER_DENIED');
// Malformed and forged identities, speeds, physical height, negative zero,
// stale movement evidence and unknown stage are rejected, not displayed.
rejected(row('PLANET_SURFACE_POSE',7,0,200,17,36,32001,0,0,30,82001,30,22));
rejected(row('PLANET_SURFACE_POSE',7,0,200,17,36,'-0',0,0,30,82001,30,22));
rejected(row('PLANET_SURFACE_POSE',7,0,200,17,36,0,32768001,0,30,82001,30,22));
rejected(row('PLANET_SURFACE_POSE',7,0,200,17,36,0,0,0,31,82001,30,22));
rejected(row('PLANET_SURFACE_POSE',7,0,200,17,36,0,0,0,0,82001,30,22));
rejected(row('PLANET_SURFACE_POSE',7,0,200,36,36,0,0,0,30,82001,30,22));
rejected(row('PLANET_PLAYER_COMMITTED',7,200,17,36,'TELEPORT',81102,20));
rejected(row('PLANET_PLAYER_STATUS',7,100,8,200,17,36,1,81234,81234,0,81102,30,21));
rejected(row('PLANET_PLAYER_STATUS',7,100,7,0,17,36,1,81234,81234,0,81102,30,21));
rejected(row('PLANET_PLAYER_REGISTERED',7,100,19,'EXTRA'));
console.log('S6-F5 strict owner-scoped original surface packet decode source PASS');
