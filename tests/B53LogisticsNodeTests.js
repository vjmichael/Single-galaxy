'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const api=require('../viewer/contract.js');
const data=fs.readFileSync(path.join(__dirname,'../recordings/b53_station_trade.sgview'),'utf8');
const frames=api.decodeFrames(data);
const states=api.applyFrames(frames);
test('B5.3 full station trade recording remains verifiable',()=>{
 assert.equal(frames.length,126);
 assert.equal(states[0].markets.get(1).price,2);
 assert.equal(states[0].markets.get(4).price,5);
 assert.equal(states.at(-1).wallets.get(7),100);
 assert.equal(states.at(-1).markets.get(4).ore,20);
 assert.equal(states.at(-1).markets.get(4).credits,900);
 assert.equal(states.at(-1).hangars.get('4:7').ore,0);
 assert.equal(states.at(-1).entities.get(2).cargo,0);
 assert.equal(states.at(-1).docks.get(2).station,4);
 assert.ok(states.some(s=>s.hangars.get('1:7')?.ore===20));
 assert.ok(states.some(s=>s.entities.get(2).x===10500 && s.entities.get(2).vx===0));
});
test('B5.3 market stock and wallet event tampering cannot pass projection hash',()=>{
 const trade=frames.findIndex(f=>f.changes.some(c=>c.type==='W' && c.credits===100));
 assert.ok(trade>0);
 const modified=structuredClone(frames[trade]);
 modified.changes.find(c=>c.type==='W').credits=200;
 assert.throws(()=>api.applyNext(states[trade-1],modified),/Frame projection hash mismatch/);
 const modifiedMarket=structuredClone(frames[trade]);
 modifiedMarket.changes.find(c=>c.type==='M').ore=21;
 assert.throws(()=>api.applyNext(states[trade-1],modifiedMarket),/Frame projection hash mismatch/);
});
test('B5.3 malformed monetary fields are rejected',()=>{
 assert.throws(()=>api.decodeFrames(data.replace('W|7|100','W|7|-100')),/Non-canonical numeric field/);
 assert.throws(()=>api.applyFrames(api.decodeFrames(data.replace('M|4|20|900|5','M|4|20|900|0'))),/Invalid market/);
});
