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
window.onGalaxyLiveProjection=function(state){
 const ship=[...state.entities.values()].find(e=>e.entityType===0&&e.owner===7);
 if(!ship)return;
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
 if(source){source.close();source=null;}token=null;
 window.GalaxyLiveBridge?.reset();window.GalaxyColony?.reset();window.GalaxySurveyBridge?.reset();
 el('interactiveControls').hidden=true;el('colonyPanel').hidden=true;el('surveyPanel').hidden=true;
 el('qaPanel').hidden=true;
 message('Signed out. Colony world data is no longer available in this session.');
};
window.addEventListener('beforeunload',()=>{if(source)source.close();});
connect();
})();
