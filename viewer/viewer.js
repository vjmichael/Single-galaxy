(function () {
'use strict';
const el = id => document.getElementById(id);
const canvas = el('space'), ctx = canvas.getContext('2d');
const names = {ten:'TEN ASTEROIDS / DEPLETION',hold:'AUTOMATIC MINING / HOLD FULL',motion:'SPATIAL MOVEMENT / INERTIA'};
const descriptions = {ten:'10 asteroids × 10 ore, ship cargo capacity 100. Each tick consumes an entire asteroid.',
  hold:'One 20-ore asteroid, ship capacity 10. Mining extracts 4, 4, then 2 and automatically stops.',
  motion:'A ship receives two thrust commands and continues drifting under inertia.'};
let timeline = [], index = 0, scenario = 'ten', playing = false, lastTime = 0, since = 0;
let visualPrevious=null,visualCurrent=null,visualReceived=0,visualPeriod=100;
const monoNow=()=>typeof performance!=='undefined'?performance.now():Date.now();
function visualTransition(previous,current,period=100){visualPrevious=previous;visualCurrent=current;visualPeriod=period;visualReceived=monoNow();}
function visualState(now=monoNow()){if(!visualCurrent||!window.GalaxyInterpolation)return null;return window.GalaxyInterpolation.interpolatePositions(visualPrevious,visualCurrent,(now-visualReceived)/visualPeriod);}
let bounds = {left:-5000,right:5000,top:-5000,bottom:5000};
let cameraFollow=true,cameraZoom=1,cameraSpan=30000;
function cameraBounds(state){
 if(!window.GalaxyColonyMode||!state||!window.GalaxyFlightCamera)return;
 const api=window.GalaxyFlightCamera, entities=[...state.entities.values()];
 const ship=entities.find(e=>e.entityType===0&&e.owner===7);
 if(cameraFollow&&ship)bounds=api.viewport(ship,cameraZoom,cameraSpan);
 else {const b=api.fit(entities);bounds=api.viewport(
   {x:(b.left+b.right)/2,y:(b.top+b.bottom)/2},cameraZoom,b.right-b.left);}
}
function updateCameraLabel(){
 const status=el('cameraStatus');
 if(status)status.textContent=(cameraFollow?'Following colony ship':'Overview of visible world')+
   ' · '+cameraZoom.toFixed(2)+'× zoom';
}
window.GalaxyFlightCameraActions={
 follow(){cameraFollow=true;cameraBounds(visualState()||selectedState());updateCameraLabel();draw();},
 overview(){cameraFollow=false;cameraBounds(visualState()||selectedState());updateCameraLabel();draw();},
 zoom(factor){cameraZoom=window.GalaxyFlightCamera.clampZoom(cameraZoom*factor);
   cameraBounds(visualState()||selectedState());updateCameraLabel();draw();},
 reset(){cameraFollow=true;cameraZoom=1;cameraBounds(visualState()||selectedState());updateCameraLabel();draw();}
};
const stars = Array.from({length:95},(_,i) => ({x:(Math.sin(i*89.17)*14321.1)%1,y:(Math.sin(i*63.41)*7834.3)%1,s: i%5===0?1.5:.7}));
function error(message) { el('error').textContent = message; el('error').style.display='block'; }
function clearError() { el('error').textContent='';el('error').style.display='none'; }
function setScenario(name, raw, custom=false) {
  try {
    const parsed=GalaxyContract.decodeFrames(raw);
    const applied=GalaxyContract.applyFrames(parsed);
    const entities=applied.flatMap(f=>[...f.entities.values()]);
    const xs=entities.map(e=>e.x),ys=entities.map(e=>e.y);
    const xmin=Math.min(...xs),xmax=Math.max(...xs),ymin=Math.min(...ys),ymax=Math.max(...ys);
    const span=Math.max(xmax-xmin,ymax-ymin,4500);
    bounds={left:(xmin+xmax)/2-span*.7,right:(xmin+xmax)/2+span*.7,
      top:(ymin+ymax)/2-span*.7,bottom:(ymin+ymax)/2+span*.7};
    timeline=applied;index=0;scenario=name;playing=false;since=0;
    el('play').textContent='▶ Play';
    cameraBounds(projected);
    el('progress').max=String(timeline.length-1);
    el('sceneName').textContent=names[name]||'IMPORTED RECORDING';
    el('description').textContent=descriptions[name]||'Validated local SGVIEW v1 recording from your simulation.';
    el('sourcebadge').textContent=custom?'Imported replay · validated':'Local replay · real B3 frames';
    clearError(); update();
  } catch (e) { playing=false;el('play').textContent='▶ Play';error('Recording rejected: '+e.message); }
}
function selectedState() {return timeline[index]||null;}
function update() {
  const f=selectedState();if(!f)return;
  el('tickPill').textContent='Tick '+f.tick;el('frameLabel').textContent=`System ${f.system}  ·  seq ${f.sequence}  ·  ${f.entities.size} visible entities`;
  el('progress').value=String(index);el('indexText').textContent=`${index} / ${timeline.length-1}`;
  const entities=[...f.entities.values()];const ship=entities.find(e=>e.entityType===0);
  const rocks=entities.filter(e=>e.entityType===1);
  const total=rocks.reduce((sum,e)=>sum+e.ore,0);
  el('cargo').textContent=ship?`${ship.cargo} / ${ship.capacity}`:'—';
  el('cargoBar').style.width=ship?(100*ship.cargo/ship.capacity)+'%':'0%';
  const dock=ship?f.docks.get(ship.id):undefined;
  const dockStatus=['IN SPACE','DOCKED','UNDOCKING','DOCKING'][dock?.state||0];
  const jump=ship?(f.jumps||new Map()).get(ship.id):null;
  const jumpText=jump ? (jump.state===1?'JUMPING':'ARRIVAL CLOAK')+` (${jump.remaining} ticks)` : 'READY';
  if(el('currentSystem'))el('currentSystem').textContent=String(f.system);
  if(el('jumpState'))el('jumpState').textContent=jumpText;
  // A local hangar is relevant only while docked/transitioning at that station.
  // Never present a remote station's stored ore as the current local hangar.
  const ownedHangar=dock&&ship ? f.hangars.get(dock.station+':'+ship.owner) : null;
  if (el('dockState')) el('dockState').textContent=dockStatus+(dock?.remaining?` (${dock.remaining} ticks)`: '');
  if (el('hangarOre')) el('hangarOre').textContent=ownedHangar?`${ownedHangar.ore} / ${ownedHangar.capacity}`:'—';
  const nearestStation = ship ? entities.filter(e=>e.entityType===2).sort((a,b)=>
    (a.x-ship.x)**2+(a.y-ship.y)**2-(b.x-ship.x)**2-(b.y-ship.y)**2)[0]?.id : undefined;
  const market=f.markets?.get(dock?.station||nearestStation);
  if(el('walletCredits')) el('walletCredits').textContent=String(f.wallets?.get(ship?.owner)||0);
  if(el('marketPrice')) el('marketPrice').textContent=market?market.price+' credits / ore':'—';
  if(el('marketStock')) el('marketStock').textContent=market?market.ore+' ore · '+market.credits+' credits':'—';
  const full=ship&&ship.cargo===ship.capacity;
  el('status').textContent=jump?jumpText:dock?dockStatus:full?'HOLD FULL':f.mining.size?'MINING':'IDLE';
  el('status').style.color=full?'#ffc773':f.mining.size?'#70e9fc':'#96acc5';
  el('position').textContent=ship?`${ship.x.toLocaleString()}, ${ship.y.toLocaleString()}`:'—';
  el('velocity').textContent=ship?`${ship.vx.toLocaleString()}, ${ship.vy.toLocaleString()}`:'—';
  el('asteroids').textContent=String(rocks.length);el('ore').textContent=String(total);
  const log=[];
  for(let k=1;k<=index;++k) for(const c of timeline[k].notifications) {
    if(c.type==='A') {
      const label=GalaxyContract.kinds[c.kind];const result=GalaxyContract.outcomes[c.result];
      log.push({text:`T${c.tick} · ${label.replaceAll('_',' ')}${c.kind===2?' · '+result:''}${c.kind===1?' · +'+c.transferred+' ore':''}`,important:c.kind===2});
    } else if(c.type==='C') {
      log.push({text:`T${c.tick} · Command ${c.command}: ${GalaxyContract.outcomes[c.result]}`+
         (c.transferred>0?` · ${c.transferred} ore transferred`:''),important:c.result!==0});
    }
  }
  el('log').replaceChildren(...(log.length?log.slice(-7).reverse().map(x=>{const d=document.createElement('div');d.className='entry'+(x.important?' important':'');d.textContent=x.text;return d;}):[Object.assign(document.createElement('div'),{className:'entry',textContent:'No gameplay notifications yet.'})]));
  let message='';const stopped=f.notifications.find(c=>c.type==='A'&&c.kind===2);
  const lastRemoval=f.notifications.filter(c=>c.type==='A'&&c.kind===0).length;
  if(stopped) message='MINING STOPPED · '+GalaxyContract.outcomes[stopped.result];
  else if(lastRemoval)message=lastRemoval+' ASTEROID'+(lastRemoval>1?'S':'')+' DEPLETED';
  else if(f.mining.size)message='AUTOMATIC MINING ACTIVE';
  el('toast').textContent=message;
  draw();
}
function transform(w,h) {
  const bw=bounds.right-bounds.left,bh=bounds.bottom-bounds.top;
  const scale=Math.min((w-100)/bw,(h-100)/bh);
  const ox=w/2-(bounds.left+bounds.right)*scale/2;
  const oy=h/2-(bounds.top+bounds.bottom)*scale/2;
  return (x,y)=>({x:ox+x*scale,y:oy+y*scale});
}
function resize() {
  const rect=canvas.getBoundingClientRect();const dpr=Math.min(window.devicePixelRatio||1,2);
  canvas.width=Math.max(1,Math.round(rect.width*dpr));canvas.height=Math.max(1,Math.round(rect.height*dpr));
  ctx.setTransform(dpr,0,0,dpr,0,0);draw();
}
function draw() {
  const state=visualState()||selectedState();if(!state)return;
  const w=canvas.clientWidth,h=canvas.clientHeight;if(!w||!h)return;
  ctx.clearRect(0,0,w,h);
  const gradient=ctx.createRadialGradient(w*.45,h*.4,0,w*.5,h*.5,Math.max(w,h)*.8);
  gradient.addColorStop(0,'#10233a');gradient.addColorStop(.55,'#0b1627');gradient.addColorStop(1,'#050914');
  ctx.fillStyle=gradient;ctx.fillRect(0,0,w,h);
  for(const s of stars){const x=((s.x+1)%1)*w,y=((s.y+1)%1)*h;ctx.fillStyle='rgba(191,218,255,.42)';ctx.fillRect(x,y,s.s,s.s);}
  cameraBounds(state);
  const xy=transform(w,h);
  ctx.strokeStyle='rgba(96,136,177,.09)';ctx.lineWidth=1;
  const origin=xy(0,0);const step=70;
  for(let x=origin.x%step;x<w;x+=step){ctx.beginPath();ctx.moveTo(x,0);ctx.lineTo(x,h);ctx.stroke();}
  for(let y=origin.y%step;y<h;y+=step){ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(w,y);ctx.stroke();}
  const entities=[...state.entities.values()];
  for (const [actor, job] of state.mining) {
    const ship=state.entities.get(actor),rock=state.entities.get(job.target);
    if(!ship||!rock)continue;
    const a=xy(ship.x,ship.y),b=xy(rock.x,rock.y);
    ctx.save();ctx.strokeStyle='rgba(99,242,226,.72)';ctx.lineWidth=2;ctx.setLineDash([5,7]);
    ctx.beginPath();ctx.moveTo(a.x,a.y);ctx.lineTo(b.x,b.y);ctx.stroke();ctx.restore();
  }
  for(const e of entities.filter(e=>e.entityType===2)) {
    const p=xy(e.x,e.y);ctx.save();ctx.translate(p.x,p.y);ctx.strokeStyle='#bea7ff';ctx.lineWidth=2;
    ctx.beginPath();for(let k=0;k<6;k++){const a=k*Math.PI/3;const x=17*Math.cos(a),y=17*Math.sin(a);k?ctx.lineTo(x,y):ctx.moveTo(x,y);}ctx.closePath();ctx.stroke();
    ctx.beginPath();ctx.moveTo(-24,0);ctx.lineTo(24,0);ctx.moveTo(0,-24);ctx.lineTo(0,24);ctx.stroke();ctx.restore();label('STATION',p.x,p.y+35,'#ad9dda');
  }
  for(const e of entities.filter(e=>e.entityType===1)) {
    const p=xy(e.x,e.y);const geo=window.GalaxyColonyGeology?.get(e.id);
    const r=geo?Math.min(65,12+Math.sqrt(geo.radius)*4):12+Math.min(9,Math.sqrt(e.ore));
    ctx.save();ctx.shadowColor='rgba(252,188,110,.42)';ctx.shadowBlur=12;ctx.fillStyle='#77594c';ctx.strokeStyle='#dfb583';ctx.lineWidth=1.5;
    ctx.beginPath();for(let k=0;k<8;k++){const a=k*Math.PI/4;const radius=r*(.82 + ((k*11+Number(e.id)*3)%7)*.055);const x=Math.cos(a)*radius,y=Math.sin(a)*radius;k?ctx.lineTo(p.x+x,p.y+y):ctx.moveTo(p.x+x,p.y+y);}ctx.closePath();ctx.fill();ctx.stroke();ctx.restore();
    if(window.GalaxyColonyMode&&e.id===window.GalaxySelectedRockId){
      ctx.save();ctx.strokeStyle='#6ee7ff';ctx.lineWidth=3;ctx.setLineDash([5,4]);
      ctx.beginPath();ctx.arc(p.x,p.y,r+10,0,Math.PI*2);ctx.stroke();ctx.restore();
    }
    label(geo?`${(e.ore/1e6).toFixed(2)}M t · ${['C','S','M'][geo.classId]}`:`${e.ore} ore`,p.x,p.y+r+18,'#ecbd8c');
  }
  for(const e of entities.filter(e=>e.entityType===3)) {
    const p=xy(e.x,e.y);ctx.save();ctx.strokeStyle='#7ed6a5';ctx.lineWidth=2;
    ctx.beginPath();ctx.arc(p.x,p.y,19,0,Math.PI*2);ctx.stroke();
    ctx.setLineDash([4,3]);ctx.beginPath();ctx.arc(p.x,p.y,12,0,Math.PI*2);ctx.stroke();ctx.restore();
    label('JUMP GATE',p.x,p.y+31,'#9ce8be');
  }
  for(const e of entities.filter(e=>e.entityType===0)) {
    const heading=window.GalaxyColonyMode&&e.owner===7?window.GalaxyShipHeading:null;
    const p=xy(e.x,e.y),angle=Number.isInteger(heading)?heading*Math.PI/180:Math.atan2(e.vy,e.vx);
    ctx.save();ctx.translate(p.x,p.y);ctx.rotate(Number.isInteger(heading)?angle:(e.vx||e.vy?angle:0));
    ctx.fillStyle='#56dcef';ctx.shadowColor='#5fe0fc';ctx.shadowBlur=16;
    ctx.beginPath();ctx.moveTo(21,0);ctx.lineTo(-13,-11);ctx.lineTo(-8,0);ctx.lineTo(-13,11);ctx.closePath();ctx.fill();
    ctx.shadowBlur=0;ctx.strokeStyle='#b9f6ff';ctx.lineWidth=1;ctx.stroke();ctx.restore();
    ctx.strokeStyle='rgba(98,225,245,.18)';ctx.beginPath();ctx.arc(p.x,p.y,39,0,Math.PI*2);ctx.stroke();
    label(window.GalaxyColonyMode&&e.owner===7?'COLONY SHIP':'MINING SHIP',p.x,p.y-28,'#b8f7ff');
  }
  ctx.font='11px monospace';ctx.fillStyle='#6584a4';ctx.textAlign='left';ctx.fillText('GALAXY / SYSTEM '+state.system+' / TICK '+state.tick,16,h-19);
}
function label(text,x,y,color) {ctx.textAlign='center';ctx.font='11px system-ui, sans-serif';ctx.fillStyle=color;ctx.fillText(text,x,y);}
function go(n) {index=Math.max(0,Math.min(timeline.length-1,n));update();}
function togglePlay() {if(!timeline.length)return;if(index===timeline.length-1)go(0);playing=!playing;el('play').textContent=playing?'Ⅱ Pause':'▶ Play';since=0;}
function animate(timestamp) {
  if(playing&&timeline.length){if(!lastTime)lastTime=timestamp;since+=timestamp-lastTime;
    const interval=770/Number(el('speed').value);
    if(since>=interval){since%=interval;if(index>=timeline.length-1){playing=false;el('play').textContent='▶ Play';}else go(index+1);}}
  if(visualCurrent)draw();
  lastTime=timestamp;requestAnimationFrame(animate);
}
el('play').addEventListener('click',togglePlay);
el('back').addEventListener('click',()=>{playing=false;el('play').textContent='▶ Play';go(index-1);});
el('next').addEventListener('click',()=>{playing=false;el('play').textContent='▶ Play';go(index+1);});
el('progress').addEventListener('input',()=>{playing=false;el('play').textContent='▶ Play';go(Number(el('progress').value));});
el('scenario').addEventListener('change',()=>{const name=el('scenario').value;setScenario(name,window.GalaxyRecordings[name]);});
el('import').addEventListener('change',async e=>{const f=e.target.files[0];if(!f)return;try{if(f.size>16*1024*1024)throw new Error('Recording limit 16 MiB');setScenario('custom',await f.text(),true);}catch(ex){error(ex.message);}e.target.value='';});
document.addEventListener('keydown',e=>{if(window.GalaxyColonyMode)return;if(e.target.closest('select,input,button'))return;if(e.code==='Space'){e.preventDefault();togglePlay();}if(e.code==='ArrowRight'){e.preventDefault();go(index+1);}if(e.code==='ArrowLeft'){e.preventDefault();go(index-1);}});
// B4 local real-time relay supplies exactly one canonical B3 frame per SSE message.
// The browser never issues game commands; the relay only exposes a read-only feed.
let liveState=null;
window.GalaxyLiveBridge={
  reset(){liveState=null;timeline=[];index=0;visualPrevious=null;visualCurrent=null;playing=false;el('play').textContent='▶ Play';el('sourcebadge').textContent='LIVE · connecting';},
  accept(raw){
    const decoded=GalaxyContract.decodeFrames(raw);
    if(decoded.length!==1)throw new Error('Live transport must contain exactly one SGVIEW frame');
    const candidate=decoded[0];
    if(!liveState && candidate.kind!==1)throw new Error('Live stream must begin with a snapshot');
    if(liveState && candidate.kind===1 &&
       !(candidate.system!==liveState.system && candidate.stream===liveState.stream &&
         candidate.sequence===liveState.sequence+1 && candidate.tick===liveState.tick+1))
      throw new Error('Unexpected or stale system snapshot');
    if(timeline.length>=1201)throw new Error('Live demo frame budget exceeded');
    const projected=GalaxyContract.applyNext(liveState,candidate); // validates hash and ordering before publication
    const previous=liveState;liveState=projected;timeline.push(projected);index=timeline.length-1;
    if(previous && previous.system!==projected.system)
      window.GalaxySurveyBridge?.systemChanged(projected.system);
    visualTransition(previous,projected,window.GalaxyTickMs||100);
    window.onGalaxyLiveProjection?.(projected);
    const entities=timeline.filter(f=>f.system===projected.system).flatMap(f=>[...f.entities.values()]);
    const xs=entities.map(e=>e.x),ys=entities.map(e=>e.y);
    const xmin=Math.min(...xs),xmax=Math.max(...xs),ymin=Math.min(...ys),ymax=Math.max(...ys);
    const span=Math.max(xmax-xmin,ymax-ymin,4500);
    bounds={left:(xmin+xmax)/2-span*.7,right:(xmin+xmax)/2+span*.7,
      top:(ymin+ymax)/2-span*.7,bottom:(ymin+ymax)/2+span*.7};
    el('progress').max=String(timeline.length-1);
    el('sceneName').textContent='LIVE · '+(names[el('scenario').value]||'SPACE SIMULATION');
    el('description').textContent='Real committed C++ simulation steps, streamed live over local loopback.';
    el('sourcebadge').textContent='LIVE · SQLite committed';
    clearError();update();
  },
  fail(message){error(message);el('sourcebadge').textContent='LIVE · disconnected';},
  complete(){el('sourcebadge').textContent='LIVE · run completed';}
};
canvas.addEventListener('click',event=>{
 if(!window.GalaxyColonyMode||!window.GalaxyFlightCamera)return;
 const view=visualState()||selectedState();if(!view)return;
 const rect=canvas.getBoundingClientRect();
 const id=window.GalaxyFlightCamera.pickRock([...view.entities.values()],
   event.clientX-rect.left,event.clientY-rect.top,bounds,rect.width,rect.height);
 if(id!==null && !window.GalaxySelectAsteroid?.(id))
   window.GalaxyTargetFeedback?.('Asteroid #'+id+' is visible but not in your scanned target roster. Scan first.');
});
canvas.addEventListener('wheel',event=>{
 if(!window.GalaxyColonyMode||!window.GalaxyFlightCamera)return;
 event.preventDefault();
 window.GalaxyFlightCameraActions.zoom(event.deltaY<0?1.2:1/1.2);
},{passive:false});
window.addEventListener('resize',resize);
setScenario('ten',window.GalaxyRecordings?.ten||'');
resize();requestAnimationFrame(animate);
})();
