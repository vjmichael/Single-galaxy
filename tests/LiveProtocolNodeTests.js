'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const api=require('../viewer/contract.js');
const root=path.join(__dirname,'..');
for (const [name,ticks,ore,cargo] of [['ten',10,0,100],['hold',7,10,10],['motion',12,20,0]]) {
  test('real live '+name+' stream validates all incremental frames',()=>{
    const raw=fs.readFileSync(path.join(root,name+'.live.sgview'),'utf8');
    const frames=api.decodeFrames(raw);
    const timeline=api.applyFrames(frames);
    assert.equal(frames.length,ticks+1);
    assert.equal(timeline[timeline.length-1].tick,ticks);
    const end=timeline[timeline.length-1];
    assert.equal([...end.entities.values()].filter(e=>e.entityType===1).reduce((n,e)=>n+e.ore,0),ore);
    assert.equal([...end.entities.values()].find(e=>e.entityType===0).cargo,cargo);
    if(name==='ten') assert.equal([...end.entities.values()].filter(e=>e.entityType===1).length,0);
    if(name==='hold') assert.ok(timeline[3].notifications.some(c=>c.type==='A'&&c.kind===2&&api.outcomes[c.result]==='CARGO_FULL'));
  });
}
test('missed or reordered real-time frame is rejected rather than silently shown',()=>{
  const data=api.decodeFrames(fs.readFileSync(path.join(root,'hold.live.sgview'),'utf8'));
  assert.throws(()=>api.applyFrames(data.filter((_,i)=>i!==2)),/Missing or out-of-order delta/);
  assert.throws(()=>api.applyFrames([data[1]]),/Missing or out-of-order delta/);
});
test('tampered entity update is rejected before client state publication',()=>{
  const data=api.decodeFrames(fs.readFileSync(path.join(root,'hold.live.sgview'),'utf8'));
  const changed=structuredClone(data);
  const upsert=changed[2].changes.find(c=>c.type==='E'&&c.entityType===0);
  assert.ok(upsert);
  upsert.cargo=999;
  assert.throws(()=>api.applyFrames(changed),/Invalid ship|Invalid entity|hash mismatch/);
});
