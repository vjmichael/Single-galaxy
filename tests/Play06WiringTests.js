const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path');
const root=path.resolve(__dirname,'..');
const source=name=>fs.readFileSync(path.join(root,name),'utf8');
const html=source('viewer/interactive.html');
const viewer=source('viewer/viewer.js');
const colony=source('viewer/colony.js');
const relay=source('live/interactive_relay.py');
test('camera controls and native scanned-target selection are wired into browser',()=>{
 for(const id of ['flightCameraControls','cameraFollow','cameraOverview','cameraZoomIn','cameraZoomOut','cameraReset','cameraStatus'])
   assert.ok(html.includes('id="'+id+'"'),id+' missing');
 assert.ok(html.includes('<script src="flight-camera.js"></script>'));
 assert.ok(relay.includes("'/viewer/flight-camera.js':'viewer/flight-camera.js'"));
 assert.ok(viewer.includes('window.GalaxyFlightCamera.pickRock'));
 assert.ok(viewer.includes('window.GalaxySelectAsteroid?.(id)'));
 assert.ok(colony.includes('small.has(id)&&!geology.has(id)'));
 assert.ok(colony.includes("new Option('Select a scanned rock'"));
});
test('live piloting controls remain distinct from replay keyboard',()=>{
 assert.ok(viewer.includes('if(window.GalaxyColonyMode)return;'));
 assert.ok(viewer.includes('ctx.rotate(Number.isInteger(heading)?angle'));
 assert.ok(viewer.includes('window.GalaxyFlightCameraActions.zoom('));
 assert.ok(html.includes('id="showOpening"'));
});
test('camera system has no server command authority or cargo mutation',()=>{
 const camera=source('viewer/flight-camera.js');
 for(const forbidden of ['/command','fetch(','.cargo=','.ore=','localStorage'])
   assert.ok(!camera.includes(forbidden),'camera should not have world write authority: '+forbidden);
});
