/* B4.3 live browser: no player IDs, target IDs or authority supplied by UI. */
(function(){'use strict';
let source=null,token=null, colonyMode=false;
let assist=null,assistPending=false,latestProjection=null;
function assistMessage(mode,description='destination'){
 const label=el('assistStatus');
 if(label)label.textContent=window.GalaxyTravelAssist?.statusText(mode,description)||mode;
 if(el('assistCancel'))el('assistCancel').disabled=!assist;
}
window.GalaxyAssistNativeOutcome=outcome=>{if(assist&&outcome!=='APPLIED')cancelAssist('paused');};
function cancelAssist(mode='cancelled'){
 const old=assist;assist=null;assistPending=false;
 if(old)assistMessage(mode,old.description);
 else if(mode!=='cancelled')assistMessage(mode);
}
function startAssist(kind){
 const frame=latestProjection;
 if(!colonyMode||!token||!frame){message('Waiting for a verified live world frame.');return;}
 const player=[...frame.entities.values()].find(e=>e.entityType===0&&e.owner===7);
 if(!player){message('Colony ship is not in this system.');return;}
 let id,description;
 if(kind==='rock'){
   id=window.GalaxySelectedRockId;
   if(!Number.isSafeInteger(id)||id<1){message('Scan and select an asteroid first.');return;}
   description='scanned asteroid #'+id;
 }else{
   const cargo=window.GalaxyCargoReturn?.plan(frame,7,window.GalaxyColonyOutpostIds);
   id=cargo?.site?.id;description='outpost #'+id;
 }
 const target=window.GalaxyTravelAssist?.target(frame,kind,id,window.GalaxyColonyOutpostIds);
 if(!target||target.system!==player.system){
   message('Travel target is not a verified, visible destination in this system.');return;
 }
 const dock=frame.docks.get(player.id)?.state??0;
 if(dock!==0){message('Undock before using travel assist.');return;}
 assist={kind,id,description,startedTick:frame.tick,lastTick:-1,sessionToken:token,shipId:player.id};
 assistMessage('travel',description);
}
async function updateAssist(frame){
 const current=assist;
 if(!current||assistPending||!token||current.sessionToken!==token)return;
 const player=frame.entities.get(current.shipId);
 const dest=window.GalaxyTravelAssist?.target(frame,current.kind,current.id,window.GalaxyColonyOutpostIds);
 if(!player||!dest||player.owner!==7||player.system!==dest.system||
    frame.tick-current.startedTick>window.GalaxyTravelAssist.MAX_TICKS){
    cancelAssist('paused');return;
 }
 const dock=frame.docks.get(player.id)?.state??0;
 const instruction=window.GalaxyTravelAssist.plan(player,dest,{
   dockState:dock,jumpActive:!!frame.jumps?.get(player.id),tolerance:500
 });
 if(instruction.status==='arrived'){cancelAssist('arrived');return;}
 if(instruction.status!=='thrust'){cancelAssist('paused');return;}
 if(frame.tick<=current.lastTick)return;
 // At most one queued native acceleration request for each committed frame.
 current.lastTick=frame.tick;assistPending=true;
 const accepted=await command('thrust',instruction.command.extra,true);
 assistPending=false;
 if(assist!==current)return;
 if(!accepted)cancelAssist('paused');
}
window.GalaxyColonyMode=false;
const el=id=>document.getElementById(id);
const message=text=>{el('commandMessage').textContent=text;};
async function command(action,extra={},assisted=false){
 if(!assisted&&assist)cancelAssist('cancelled');
 if(!token){message('No local session available');return false;}
 try{
   const response=await fetch('/command',{method:'POST',headers:{'Content-Type':'application/json'},
     body:JSON.stringify({token,action,...extra})});
   const result=await response.json();
   if(!response.ok)throw new Error(result.error||'Command rejected');
   if(!assisted)message('Command queued (not yet committed); next simulation tick will decide.');
   return true;
 }catch(ex){message('Command not accepted: '+ex.message);return false;}
}
async function connect(){
 cancelAssist('paused');latestProjection=null;
 if(source){source.close();source=null;}
 window.GalaxyColonyMode=false;
 window.GalaxyResetExpedition?.();
 const bridge=window.GalaxyLiveBridge;bridge.reset();window.GalaxySurveyBridge?.reset();window.GalaxyColony?.reset();message('Connecting to one running local world…');
 try{
   const response=await fetch('/session',{cache:'no-store'});
   if(!response.ok)throw new Error('Session unavailable');
   const data=await response.json();token=data.token;window.GalaxyTickMs=data.tick_ms;
   colonyMode=!!data.colony_mode;
   window.GalaxyColonyMode=colonyMode;
   el('qaPanel').hidden=!data.qa_mode;
   el('interactiveControls').hidden=colonyMode;
   el('surveyPanel').hidden=colonyMode;
   el('colonyPanel').hidden=!colonyMode;
   el('flightCameraControls').hidden=!colonyMode;
   if(colonyMode)window.GalaxyFlightCameraActions?.reset();
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
   source.addEventListener('complete',()=>{cancelAssist('paused');bridge.complete();message('Run completed. Reconnect to review the finished world.');source.close();source=null;});
   source.addEventListener('failed',e=>{cancelAssist('paused');bridge.fail('Host failed: '+e.data);source.close();source=null;if(e.data==='Session expired')window.GalaxyDisconnect?.();});
   source.addEventListener('error',()=>{if(source){cancelAssist('paused');source.close();source=null;bridge.fail('Stream lost. Use Reconnect to replay and catch up.');}});
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
let expedition=window.GalaxyExpeditionFeedback?.initial();
window.GalaxyResetExpedition=()=>{
 expedition=window.GalaxyExpeditionFeedback?.initial();
 el('expeditionMined').textContent='0 t';
 el('expeditionDelivered').textContent='0 t';
 el('expeditionTarget').textContent='—';
 el('expeditionStatus').textContent='Waiting for committed ship and outpost state.';
 el('expeditionOutcome').textContent='Awaiting an actual native result';
};
el('returnBrake').addEventListener('click',()=>{
 if(!lastReturnShip)return;
 const x=Math.sign(lastReturnShip.vx||0),y=Math.sign(lastReturnShip.vy||0);
 if(!x&&!y){message('Ship already at zero velocity.');return;}
 command('thrust',{x:-x,y:-y});
});
for(const [id,method,arg] of [
 ['cameraFollow','follow'],['cameraOverview','overview'],
 ['cameraZoomIn','zoom',1.2],['cameraZoomOut','zoom',1/1.2],
 ['cameraReset','reset']
])el(id).addEventListener('click',()=>window.GalaxyFlightCameraActions?.[method]?.(arg));
window.GalaxyTargetFeedback=text=>message(text);
el('assistRock').addEventListener('click',()=>startAssist('rock'));
el('assistHome').addEventListener('click',()=>startAssist('home'));
el('assistCancel').addEventListener('click',()=>cancelAssist());
el('salvageTarget').addEventListener('change',()=>{
 el('salvageRecover').disabled=!el('salvageTarget').value;
});
el('salvageRecover').addEventListener('click',()=>{
 const target=Number(el('salvageTarget').value);
 if(!Number.isSafeInteger(target)||target<=0){message('Choose a reported physical wreck first.');return;}
 command('colonySalvageWreck',{target,amount:6});
});
el('combatTarget').addEventListener('change',()=>{
 el('combatFire').disabled=!el('combatTarget').value||el('combatCharges').textContent==='0';
});
el('combatFire').addEventListener('click',()=>{
 const target=Number(el('combatTarget').value);
 if(!Number.isSafeInteger(target)||target<=0){message('Choose a native training drone first.');return;}
 command('colonyFireTraining',{target});
});
el('flightScan').addEventListener('click',()=>command('colonyScan'));
el('explorationScan').addEventListener('click',()=>command('colonyScan'));
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
 latestProjection=state;
 if(colonyMode&&assist)void updateAssist(state);
 const ship=[...state.entities.values()].find(e=>e.entityType===0&&e.owner===7);
 if(!ship)return;
 const wreckId=Number(el('salvageTarget').value);
 const wreck=Number.isSafeInteger(wreckId)&&wreckId>0?state.entities.get(wreckId):null;
 if(wreck?.entityType===4){
   const distance=Math.hypot(wreck.x-ship.x,wreck.y-ship.y);
   const inRange=distance<=2500;
   el('salvageRecover').disabled=!inRange||ship.cargo>=ship.capacity||
     !(window.GalaxySalvageStock?.get(wreckId)>0);
   el('salvageFeedback').textContent='Wreck #'+wreckId+' · '+Math.ceil(distance)+' mm away.'+
      (inRange?' Within salvage reach.':' Pilot within 2,500 mm to recover stock.');
 }
 const combatId=Number(el('combatTarget').value);
 const opponent=Number.isSafeInteger(combatId)&&combatId>0?state.entities.get(combatId):null;
 if(opponent?.entityType===4){
   el('combatFeedback').textContent='Training drone #'+combatId+' destroyed; physical wreck remains in the galaxy.';
   el('combatFire').disabled=true;
 }else if(opponent?.entityType===0){
   const dist=Math.hypot(opponent.x-ship.x,opponent.y-ship.y);
   el('combatFire').disabled=dist>12000||el('combatCharges').textContent==='0';
 }
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
 const chosen=Number(el('colonyRockTarget').value);
 const selected=Number.isSafeInteger(window.GalaxySelectedRockId)&&window.GalaxySelectedRockId>0?
   window.GalaxySelectedRockId:(Number.isSafeInteger(chosen)&&chosen>0?chosen:null);
 const mine=window.GalaxyExpeditionFeedback?.target(ship,nearby,selected);
 el('assistRock').disabled=!colonyMode||!selected||!window.GalaxyTravelAssist?.target(
    state,'rock',selected,window.GalaxyColonyOutpostIds);
 const docked=state.docks.get(ship.id)?.state??0;
 const home=window.GalaxyCargoReturn?.plan(state,7,window.GalaxyColonyOutpostIds)?.site;
 el('assistHome').disabled=!colonyMode||!home||docked!==0;
 el('expeditionTarget').textContent=mine?
   '#'+mine.id+' · '+Math.ceil(mine.distance).toLocaleString()+' mm · '+mine.ore.toLocaleString()+' t visible ore':
   'No mineable asteroid currently visible';
 const cargo=window.GalaxyCargoReturn?.plan(state,7,window.GalaxyColonyOutpostIds);
 lastReturnShip=ship;
 if(cargo?.site && window.GalaxyExpeditionFeedback){
   const homeBay=cargo.bayOre;
   const valid=Number.isSafeInteger(homeBay)&&Number.isSafeInteger(ship.cargo)&&
     Number.isSafeInteger(state.tick);
   if(valid){
     expedition=window.GalaxyExpeditionFeedback.advance(expedition,{
       tick:state.tick,hold:ship.cargo,bay:homeBay,home:cargo.site.id,
       docked:cargo.dockState===1&&state.docks.get(ship.id)?.station===cargo.site.id
     });
     el('expeditionMined').textContent=expedition.mined+' t';
     el('expeditionDelivered').textContent=expedition.delivered+' t';
     el('expeditionStatus').textContent=window.GalaxyExpeditionFeedback.status(expedition);
   }
 }else if(cargo?.ready){
   el('expeditionStatus').textContent='No player-built home outpost visible. Complete your first settlement before a full cargo-return expedition.';
 }
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
 cancelAssist('paused');latestProjection=null;
 lastReturnShip=null;
 if(source){source.close();source=null;}token=null;
 colonyMode=false;window.GalaxyColonyMode=false;
 window.GalaxyLiveBridge?.reset();window.GalaxyColony?.reset();window.GalaxySurveyBridge?.reset();
 el('interactiveControls').hidden=true;el('colonyPanel').hidden=true;el('surveyPanel').hidden=true;
 el('flightCameraControls').hidden=true;
 el('qaPanel').hidden=true;
 message('Signed out. Colony world data is no longer available in this session.');
};
window.addEventListener('beforeunload',()=>{cancelAssist('paused');if(source)source.close();});
connect();
})();
