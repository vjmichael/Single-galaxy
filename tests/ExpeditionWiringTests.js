const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const root=path.resolve(__dirname,'..');
const html=fs.readFileSync(path.join(root,'viewer/interactive.html'),'utf8');
const relay=fs.readFileSync(path.join(root,'live/interactive_relay.py'),'utf8');
const scripts=['opening-guide.js','flight-controls.js','cargo-return.js','expedition-feedback.js'];
test('every live gameplay helper is referenced by the browser and allowlisted by relay',()=>{
 for(const name of scripts){
   assert.ok(html.includes('<script src="'+name+'"></script>'),name+' missing browser reference');
   assert.ok(relay.includes("'/viewer/"+name+"':'viewer/"+name+"'"),name+' missing relay static file');
   assert.ok(fs.existsSync(path.join(root,'viewer',name)),name+' missing on disk');
 }
});
test('expedition UI has all required accessible feedback elements',()=>{
 for(const id of ['expeditionStatus','expeditionMined','expeditionDelivered',
                   'expeditionTarget','expeditionOutcome'])
   assert.ok(html.includes('id="'+id+'"'),id+' missing');
 assert.match(html,/aria-live="polite"/);
});
test('cargo workflow remains an explicit native action, not client-side ore mutation',()=>{
 const js=fs.readFileSync(path.join(root,'viewer/interactive.js'),'utf8');
 for(const action of ['colonyDockBase','colonyUnloadBase','colonyLoadBase','colonyUndockBase'])
   assert.ok(js.includes("'"+action+"'"),action+' disconnected');
 assert.ok(relay.includes("'"+'colonyUnloadBase'+"'"));
 assert.ok(js.includes('GalaxyExpeditionFeedback.advance'));
});
