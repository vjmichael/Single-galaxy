const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..');
const read=name=>fs.readFileSync(path.join(root,name),'utf8');
const html=read('viewer/interactive.html');
const relay=read('live/interactive_relay.py');
const interactive=read('viewer/interactive.js');
test('PLAY-07 assistant script is served and UI has explicit opt-in/cancel',()=>{
 assert.ok(html.includes('<script src="travel-assist.js"></script>'));
 assert.ok(relay.includes("'/viewer/travel-assist.js':'viewer/travel-assist.js'"));
 for(const id of ['assistRock','assistHome','assistCancel','assistStatus'])
   assert.ok(html.includes('id="'+id+'"'),id);
 assert.match(html,/uses the ship’s existing inertia/);
});
test('assist only uses authenticated native thrust and one command per projected tick',()=>{
 assert.ok(interactive.includes("command('thrust',instruction.command.extra,true)"));
 assert.ok(interactive.includes("if(frame.tick<=current.lastTick)return;"));
 assert.ok(interactive.includes('assistPending=true'));
 assert.ok(interactive.includes('cancelAssist('));
 assert.ok(interactive.includes("if(!assisted&&assist)cancelAssist('cancelled')"));
 assert.ok(interactive.includes('frame.tick-current.startedTick>window.GalaxyTravelAssist.MAX_TICKS'));
 assert.ok(!read('viewer/travel-assist.js').includes('fetch('));
});
test('prevent earlier blocking malformed JavaScript regression and protect reconnect',()=>{
 assert.ok(!interactive.includes('let window.GalaxyResetExpedition?.();'));
 assert.ok(interactive.includes("window.GalaxyAssistNativeOutcome=outcome=>{if(assist&&outcome!=='APPLIED')"));
 assert.ok(read('viewer/colony.js').includes('window.GalaxyAssistNativeOutcome?.(e.outcome)'));
 assert.ok(interactive.includes("async function connect(){\n cancelAssist('paused')"));
 assert.ok(interactive.includes("window.GalaxyDisconnect=()=>{\n cancelAssist('paused')"));
 assert.ok(interactive.includes("source.addEventListener('error',()=>{if(source){cancelAssist('paused')"));
});
