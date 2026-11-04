/* B4.3 live browser: no player IDs, target IDs or authority supplied by UI. */
(function(){'use strict';
let source=null,token=null, colonyMode=false;
const el=id=>document.getElementById(id);
const message=text=>{el('commandMessage').textContent=text;};
async function command(action,extra={}){
 if(!token){message('No local session available');return;}
 try{
   const response=await fetch('/command',{method:'POST',headers:{'Content-Type':'application/json'},
     body:JSON.stringify({token,action,...extra})});
   const result=await response.json();
   if(!response.ok)throw new Error(result.error||'Command rejected');
   message('Command queued (not yet committed); next simulation tick will decide.');
 }catch(ex){message('Command not accepted: '+ex.message);}
}
async function connect(){
 if(source){source.close();source=null;}
 const bridge=window.GalaxyLiveBridge;bridge.reset();window.GalaxySurveyBridge?.reset();window.GalaxyColony?.reset();message('Connecting to one running local world…');
 try{
   const response=await fetch('/session',{cache:'no-store'});
   if(!response.ok)throw new Error('Session unavailable');
   const data=await response.json();token=data.token;window.GalaxyTickMs=data.tick_ms;
   colonyMode=!!data.colony_mode;
   el('qaPanel').hidden=!data.qa_mode;
   el('interactiveControls').hidden=colonyMode;
   el('surveyPanel').hidden=colonyMode;
   el('colonyPanel').hidden=!colonyMode;
   document.querySelector('.view .help').textContent=colonyMode?
     'Colony start: one ship, 24 colonists, no prebuilt infrastructure. Scan, mine, refine, fabricate, and construct your first outpost.':
     'Legacy gated region: developer-only scenario fixtures.';
   source=new EventSource('/events');
   source.addEventListener('colony',e=>{
     if(!colonyMode)return;
     try{window.GalaxyColony?.accept(atob(e.data));}
     catch(ex){message('Colony telemetry rejected: '+ex.message);}
   });
   source.addEventListener('survey',e=>{
     try{window.GalaxySurveyBridge?.accept(atob(e.data));}
     catch(ex){message('Sensor message rejected: '+ex.message);}
   });
   source.addEventListener('frame',e=>{
     try{bridge.accept(atob(e.data));}
     catch(ex){source.close();source=null;bridge.fail('Frame rejected: '+ex.message);}
   });
   source.addEventListener('complete',()=>{bridge.complete();message('Run completed. Reconnect to review the finished world.');source.close();source=null;});
   source.addEventListener('failed',e=>{bridge.fail('Host failed: '+e.data);source.close();source=null;if(e.data==='Session expired')window.GalaxyDisconnect?.();});
   source.addEventListener('error',()=>{if(source){source.close();source=null;bridge.fail('Stream lost. Use Reconnect to replay and catch up.');}});
   message(colonyMode?'Connected: colony ship in open space; no stations or gates.':'Connected: developer legacy world.');
 }catch(ex){bridge.fail(ex.message);message('Connection failed: '+ex.message);}
}
for(const [id,act,extra] of [
 ['undock','undock',{}],['dock','dock',{}],['unload','unload',{}],
 ['load','load',{}],['sell','sell',{}],['travelA','travelA',{}],['travelB','travelB',{}],['travelGate','travelGate',{}],['jump','jump',{}],['travelHop','travelHop',{}],['jumpHop','jumpHop',{}],
 ['mine4','mine',{amount:4}],
 ['probeDeploy','probeDeploy',{}],['probeScan','probeScan',{}],
 ['probeRecover','probeRecover',{}],['surveyArchive','surveyArchive',{}],['surveyHistory','surveyHistory',{}],
 ['surveyPublish','surveyPublish',{}],['stopMine','stop',{}],
 ['left','thrust',{x:-1,y:0}],['right','thrust',{x:1,y:0}],
 ['up','thrust',{x:0,y:-1}],['down','thrust',{x:0,y:1}],['drift','thrust',{x:0,y:0}]
])el(id).addEventListener('click',()=>command(act,extra));
el('planRoute').addEventListener('click',()=>command('planRoute',{destination:Number(el('routeDestination').value),policy:el('routePolicy').value}));
el('qaRoute').addEventListener('click',()=>command('qaRoute',{destination:Number(el('routeDestination').value),policy:el('routePolicy').value}));
el('qaStop').addEventListener('click',()=>command('qaStop'));
el('qaGateDown').addEventListener('click',()=>command('qaGate',{from:2,to:6,online:false}));
el('qaGateUp').addEventListener('click',()=>command('qaGate',{from:2,to:6,online:true}));
el('directional').addEventListener('click',()=>command('directional',{bearing:Number(el('scanBearing').value),cone:Number(el('scanCone').value)}));
el('reconnect').addEventListener('click',connect);
for(const [id,action] of [['colonySafetyAssess','colonySafetyAssess'],['colonySafetyPreflight','colonySafetyPreflight']])
 el(id).addEventListener('click',()=>{
   // This site was emitted by the native host from the active world.
   const site=window.GalaxyColonyHomeShipId;
   if(!Number.isSafeInteger(site)||site<1){message('Waiting for verified ship telemetry');return;}
   command(action,{site});
 });
// PLAY-03: optional pilot shortcuts and mouse controls. Native host handles physics.
for(const [id,x,y] of [['flightLeft',-1,0],['flightRight',1,0],
  ['flightUp',0,-1],['flightDown',0,1],['flightDrift',0,0]])
 el(id).addEventListener('click',()=>command('thrust',{x,y}));
el('flightYawLeft').addEventListener('click',()=>command('yaw',{delta:-15}));
el('flightYawRight').addEventListener('click',()=>command('yaw',{delta:15}));
for(const [id,act] of [
 ['returnDock','colonyDockBase'],['returnUnload','colonyUnloadBase'],
 ['returnLoad','colonyLoadBase'],['returnUndock','colonyUndockBase']
])el(id).addEventListener('click',()=>command(act));
let lastReturnShip=null;
el('returnBrake').addEventListener('click',()=>{
 if(!lastReturnShip)return;
 const x=Math.sign(lastReturnShip.vx||0),y=Math.sign(lastReturnShip.vy||0);
 if(!x&&!y){message('Ship already at zero velocity.');return;}
 command('thrust',{x:-x,y:-y});
});
el('flightScan').addEventListener('click',()=>command('colonyScan'));
el('flightMine').addEventListener('click',()=>command('colonyMine',{amount:8}));
el('flightMineSelected').addEventListener('click',()=>{
 const id=Number(el('colonyRockTarget').value);
 if(!Number.isSafeInteger(id)||id<=0){message('Scan and select a real asteroid first.');return;}
 command('colonyMineTarget',{target:id,amount:8});
});
document.addEventListener('keydown',event=>{
 if(!colonyMode||!token)return;
 const active=document.activeElement;
 const editable=!!active&&(active.isContentEditable||
  ['INPUT','TEXTAREA','SELECT','BUTTON'].includes(active.tagName));
 const mapped=window.GalaxyFlightControls?.flightInput(
   event.key,editable,event.repeat,event.altKey,event.ctrlKey,event.metaKey);
 if(!mapped)return;
 event.preventDefault();
 command(mapped.action,mapped.extra);
});
window.onGalaxyLiveProjection=function(state){
 const ship=[...state.entities.values()].find(e=>e.entityType===0&&e.owner===7);
 if(!ship)return;
 const nearby=[...state.entities.values()].filter(e=>
   e.entityType===1 && e.system===ship.system);
 const sorted=nearby.map(rock=>({rock,range:window.GalaxyFlightControls?.rockRange(ship,rock)}))
   .filter(item=>item.range!==null).sort((a,b)=>a.range-b.range);
 const first=sorted[0];
 const speed=Math.hypot(ship.vx||0,ship.vy||0);
 el('flightTelemetry').textContent=
   'Position '+ship.x.toLocaleString()+', '+ship.y.toLocaleString()+
   ' · speed '+speed.toFixed(0)+' mm/tick'+
   (first?' · nearest visible asteroid #'+first.rock.id+
      ' at '+first.range.toFixed(0)+' mm':' · no asteroid currently visible');
 const cargo=window.GalaxyCargoReturn?.plan(state,7,window.GalaxyColonyOutpostIds);
 lastReturnShip=ship;
 if(cargo){
   el('returnTelemetry').textContent=cargo.message+
     (cargo.site?' Direction to home: ΔX '+cargo.dx.toFixed(0)+' mm, ΔY '+cargo.dy.toFixed(0)+' mm.':'');
   el('returnHold').textContent=cargo.ready?ship.cargo+' t':'—';
   el('returnBay').textContent=cargo.site?cargo.bayOre+' t':'—';
   for(const [id,ready] of [['returnDock',cargo.canDock],
     ['returnUnload',cargo.canUnload],['returnLoad',cargo.canLoad],
     ['returnUndock',cargo.canUndock]])el(id).disabled=!colonyMode||!ready;
   el('returnBrake').disabled=!colonyMode||!(ship.vx||ship.vy)||cargo.dockState!==0;
 }
 const docking=state.docks.get(ship.id)?.state||0;
 el('undock').disabled=docking!==1;
 // Leave Dock enabled in open space, even when distant/moving, so authoritative
 // OUT_OF_RANGE/SHIP_MOVING rejections can be inspected in the event log.
 el('dock').disabled=docking!==0;
 const local=state.hangars.get((state.docks.get(ship.id)?.station||0)+':7');
 el('unload').disabled=docking!==1||ship.cargo===0;
 el('load').disabled=docking!==1||!local?.ore||ship.cargo>=ship.capacity;
 el('sell').disabled=docking!==1||!local?.ore;
 el('travelA').disabled=docking!==0;
 el('travelB').disabled=docking!==0;
 el('travelGate').disabled=docking!==0 || !!state.jumps?.get(ship.id);
 el('jump').disabled=docking!==0 || !!state.jumps?.get(ship.id);
 el('travelHop').disabled=docking!==0 || !!state.jumps?.get(ship.id);
 el('jumpHop').disabled=docking!==0 || !!state.jumps?.get(ship.id);
 el('mine4').disabled=docking!==0||ship.cargo===ship.capacity || !!state.jumps?.get(ship.id);
 for(const id of ['directional','probeDeploy','probeScan','probeRecover','surveyPublish']) el(id).disabled=docking!==0;
};
window.GalaxyColonyCommand=command;
window.GalaxyReconnect=connect;
window.GalaxyDisconnect=()=>{
 lastReturnShip=null;
 if(source){source.close();source=null;}token=null;
 window.GalaxyLiveBridge?.reset();window.GalaxyColony?.reset();window.GalaxySurveyBridge?.reset();
 el('interactiveControls').hidden=true;el('colonyPanel').hidden=true;el('surveyPanel').hidden=true;
 el('qaPanel').hidden=true;
 message('Signed out. Colony world data is no longer available in this session.');
};
window.addEventListener('beforeunload',()=>{if(source)source.close();});
connect();
})();
