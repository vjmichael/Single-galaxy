const test=require('node:test'),assert=require('node:assert/strict');
const {clampZoom,viewport,fit,toScreen,pickRock}=require('../viewer/flight-camera.js');
test('camera zoom is bounded and invalid input cannot corrupt projection',()=>{
 assert.equal(clampZoom(100),8);assert.equal(clampZoom(0.01),0.35);
 assert.equal(clampZoom(Infinity),1);assert.equal(clampZoom(NaN),1);
 assert.equal(viewport(null),null);
});
test('follow camera is centered on current ship and zoom changes span, not world coordinates',()=>{
 const s=viewport({x:12000,y:-8000},2,100000);
 assert.equal((s.left+s.right)/2,12000);assert.equal((s.top+s.bottom)/2,-8000);
 assert.equal(s.right-s.left,50000);
 assert.deepEqual(toScreen({x:12000,y:-8000},s,1000,600),{x:500,y:300});
});
test('fit bounds includes all committed visible entities with margin',()=>{
 const b=fit([{x:-12000,y:100},{x:6000,y:10000}]);
 assert.ok(b.left<-12000 && b.right>6000);
 assert.ok(b.top<100&&b.bottom>10000);
});
test('screen picking only selects real visible asteroids and honors distance',()=>{
 const b=viewport({x:0,y:0},1,10000),a={id:2,entityType:1,x:1000,y:0,ore:8};
 const p=toScreen(a,b,1000,700);
 assert.equal(pickRock([a],p.x,p.y,b,1000,700),2);
 assert.equal(pickRock([{...a,ore:0}],p.x,p.y,b,1000,700),null);
 assert.equal(pickRock([{...a,entityType:2}],p.x,p.y,b,1000,700),null);
 assert.equal(pickRock([a],-100,-100,b,1000,700),null);
});
