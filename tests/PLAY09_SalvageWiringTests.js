const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..');
const source=name=>fs.readFileSync(path.join(root,name),'utf8');
const {decode}=require('../viewer/colony.js');
test('native wreck stock is strictly decoded',()=>{
 assert.deepEqual(decode('COLONY|1|5|SALVAGE_WRECK|3|6'),
   {tick:5,type:'SALVAGE_WRECK',id:3,cargo:6});
 for(const row of ['COLONY|1|5|SALVAGE_WRECK|0|6','COLONY|1|5|SALVAGE_WRECK|3|-1',
   'COLONY|1|5|SALVAGE_WRECK|3|999999999','COLONY|1|5|SALVAGE_WRECK|4'])
   assert.throws(()=>decode(row),row);
});
test('UI and authorized relay reach native salvage only',()=>{
 const html=source('viewer/interactive.html'),client=source('viewer/interactive.js');
 const relay=source('live/interactive_relay.py'),host=source('src/colony_live_host.cpp');
 assert.ok(html.includes('id="salvageTarget"'));
 assert.ok(html.includes('id="salvageRecover"'));
 assert.ok(client.includes("command('colonySalvageWreck',{target,amount:6})"));
 assert.ok(relay.includes("'colonySalvageWreck'"));
 assert.ok(relay.includes('SALVAGE_WRECK|{target}|{amount}'));
 assert.ok(host.includes('c.action=Action::SalvageWreck'));
 assert.ok(host.includes('announce(w,"SALVAGE_WRECK|"'));
});
test('native wreck stock is transferred, never browser-created',()=>{
 const native=source('src/Simulation.cpp');
 assert.ok(native.includes('actor.cargo+=moved;wreck.cargo-=moved'));
 assert.ok(native.includes('case Action::SalvageWreck:'));
 assert.ok(native.includes('colony_ships_.count(c.target)'));
 assert.ok(!source('viewer/interactive.js').includes('wreck.cargo-='));
});
