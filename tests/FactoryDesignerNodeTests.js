'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {Editor,WORKSHOP,edgeAt,endpoints,edgesBetween}=require('../viewer/factory-layout.js');
const fixture=fs.readFileSync(path.join(__dirname,'fixtures/ECON02B2d4b_editor_blueprint.sglayout'),'utf8');
function example(){const e=new Editor();e.addDevice('base',0,0);e.addDevice('storage',4,0);e.addDevice('workshop',8,0);e.autoRoute(1,2,'good/aluminum',7);e.autoRoute(2,WORKSHOP,'good/aluminum',5);return e;}
function reject(fn,substring){assert.throws(fn, e=>e.message.includes(substring), 'must reject '+substring);}
test('export is byte-for-byte identical to C++ FactoryLayout canonical fixture',()=>{
 const e=example();assert.equal(e.canonical(),fixture);assert.deepEqual(e.audit(),{valid:true,errors:[],jobs:16,modules:11,devices:3,conveyors:2,segments:6,walls:0,floors:0});
});
test('walls, doors and floor tiles snap, persist and never leak into native plan',()=>{
 const e=example(),before=e.canonical();
 const horizontal=edgeAt(2.3,4.04),vertical=edgeAt(3.04,4.3);
 assert.equal(horizontal.axis,'h');assert.deepEqual(endpoints(horizontal),[{x:2,y:4},{x:3,y:4}]);
 assert.equal(vertical.axis,'v');assert.deepEqual(endpoints(vertical),[{x:3,y:4},{x:3,y:5}]);
 e.setEdgeRun({x:2,y:4},{x:5,y:4},'wall');e.setEdgeRun({x:5,y:4},{x:5,y:7},'wall');
 assert.equal(e.walls.length,6);
 e.setEdge({axis:'h',x:3,y:4},'door');e.setFloor(2,2);e.setFloor(3,2);
 assert.equal(e.walls.filter(w=>w.type==='door').length,1);assert.equal(e.floors.length,2);
 assert.equal(e.canonical(),before,'architectural overlays must not masquerade as world-built items');
 const restored=new Editor(e.exportDesign());assert.deepEqual(restored.snapshot(),e.snapshot());
 assert.equal(restored.canonical(),before);
 assert.deepEqual(edgesBetween({x:0,y:1},{x:2,y:1}),[{axis:'h',x:0,y:1},{axis:'h',x:1,y:1}]);
});
test('overlapping device ghosts and conveyor routes are rejected atomically',()=>{
 const e=example(),before=e.canonical();
 reject(()=>e.addDevice('storage',8,1),'collides');
 reject(()=>e.addDevice('workshop',10,0),'Only one');
 reject(()=>e.addDevice('storage',3,0),'collides');
 reject(()=>e.moveDevice(2,0,0),'before moving');
 reject(()=>e.addRoute(1,2,'good/aluminum',5,[{x:2,y:0}]),'Conveyor');
 assert.equal(e.canonical(),before);assert.equal(e.devices.length,3);
});
test('walls block automatic routing without door but permit doorway',()=>{
 const e=new Editor();e.addDevice('base',0,0);e.addDevice('storage',4,0);
 e.setEdgeRun({x:2,y:-100},{x:2,y:100},'wall');
 reject(()=>e.autoRoute(1,2,'good/aluminum',7),'No valid path');
 e.setEdge({axis:'v',x:2,y:0},'door');
 assert.equal(e.autoRoute(1,2,'good/aluminum',7),101);
 assert.equal(e.routes.length,1);assert.equal(e.routes[0].path.length,3);
});
test('undo redo, erasing equipment, immutable failed operations',()=>{
 const e=new Editor();e.addDevice('base',0,0);e.addDevice('storage',3,0);const snap=e.exportDesign();
 e.moveDevice(2,5,0);assert.equal(e.devices[1].x,5);
 assert.equal(e.undo(),true);assert.equal(e.devices[1].x,3);
 assert.equal(e.redo(),true);assert.equal(e.devices[1].x,5);
 assert.equal(e.eraseCell(5,0),'Equipment removed');assert.equal(e.devices.length,1);
 assert.equal(e.undo(),true);assert.equal(e.devices.length,2);
 reject(()=>e.addRoute(1,2,'good/aluminum',0,[{x:1,y:0},{x:2,y:0},{x:3,y:0},{x:4,y:0}]),'rate');
 assert.ok(snap.includes('"format"'));
});
test('malicious, duplicate and mutated design documents fail without replacing current work',()=>{
 const e=example(),saved=e.exportDesign();
 reject(()=>e.load('{"format":"v2"}'),'Unsupported');assert.equal(e.exportDesign(),saved);
 const corrupt=JSON.parse(saved);corrupt.devices[1].x=0;
 reject(()=>e.load(corrupt),'collides');assert.equal(e.exportDesign(),saved);
 const mismatch=JSON.parse(saved);mismatch.canonical+='X';
 reject(()=>e.load(mismatch),'does not match');assert.equal(e.exportDesign(),saved);
 const unknown=JSON.parse(saved);unknown.routes[0].item='good/magic';delete unknown.canonical;
 reject(()=>e.load(unknown),'Item incompatible');assert.equal(e.exportDesign(),saved);
});
test('UI is a working standalone browser editor, but does not claim world authorization',()=>{
 const html=fs.readFileSync(path.join(__dirname,'../viewer/factory.html'),'utf8');
 for(const id of ['factoryCanvas','routeItem','routeRate','saveDesign','openDesign','exportNative','auditButton'])assert.ok(html.includes(`id="${id}"`));
 for(const tool of ['base','storage','workshop','route','wall','door','floor','erase','select'])assert.ok(html.includes(`data-tool="${tool}"`));
 assert.match(html,/not connected to live colony/i);assert.match(html,/not yet buildable world objects/i);
 assert.match(fs.readFileSync(path.join(__dirname,'../viewer/factory.js'),'utf8'),/pointerdown/);
});
test('native text blueprint can be imported and re-exported exactly, even after sparse IDs',()=>{
 const a=example(),copy=new Editor().loadCanonical(fixture);
 assert.equal(copy.canonical(),fixture);
 const s=new Editor();s.addDevice('base',0,0);s.addDevice('storage',4,0);s.addDevice('storage',8,0);s.eraseCell(4,0);
 const restored=new Editor(s.exportDesign());assert.deepEqual(restored.devices.map(d=>d.id),[1,3]);
 const parsed=new Editor().loadCanonical(s.canonical());assert.equal(parsed.canonical(),s.canonical());
 reject(()=>parsed.loadCanonical(fixture+'untrusted'),'Unsupported');
 assert.equal(parsed.canonical(),s.canonical());
});
test('new solid wall cannot sever an existing conveyor; doorway can',()=>{
 const e=example(),saved=e.exportDesign();
 reject(()=>e.setEdge({axis:'v',x:2,y:0},'wall'),'cut through');
 assert.equal(e.exportDesign(),saved);
 assert.equal(e.setEdge({axis:'v',x:2,y:0},'door'),true);
});
