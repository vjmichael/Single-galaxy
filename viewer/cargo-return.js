/* PLAY-04: projection-only navigation and cargo presentation.
 * No browser authority: server owns docking, transfer and cargo conservation.
 */
(function(root,factory){'use strict';const api=factory();if(typeof module!=='undefined'&&module.exports)module.exports=api;root.GalaxyCargoReturn=api;})(typeof window!=='undefined'?window:globalThis,function(){
 'use strict';
 function plan(state,owner=7){
   if(!state||!state.entities||!state.docks||!state.hangars)
     return {ready:false,message:'Waiting for live ship and outpost projection.'};
   const ships=[...state.entities.values()].filter(e=>e.entityType===0&&e.owner===owner);
   const ship=ships.sort((a,b)=>a.id-b.id)[0];
   if(!ship)return {ready:false,message:'No player-owned ship visible.'};
   const sites=[...state.entities.values()].filter(e=>e.entityType===2&&e.owner===owner&&e.system===ship.system)
     .map(e=>({e,dx:e.x-ship.x,dy:e.y-ship.y,distance:Math.hypot(e.x-ship.x,e.y-ship.y)}))
     .filter(p=>Number.isFinite(p.distance))
     .sort((a,b)=>a.distance-b.distance||a.e.id-b.e.id);
   const site=sites[0];
   const dock=state.docks.get(ship.id);
   const dockState=dock?.state??0;
   const bay=site?state.hangars.get(site.e.id+':'+owner):null;
   const moving=!!(ship.vx||ship.vy);
   if(!site)return {ready:true,ship,site:null,dockState,canDock:false,canUnload:false,canLoad:false,
     canUndock:false, message:'No built home outpost visible. First mine, refine, fabricate and construct one.'};
   const dockedHere=dockState===1&&dock?.station===site.e.id;
   const inRange=site.distance<=2500;
   const hold=ship.cargo;
   return {
     ready:true,ship,site:site.e,distance:site.distance,dx:site.dx,dy:site.dy,
     dockState,bayOre:bay?.ore??0,hold,
     canDock:dockState===0&&inRange&&!moving,
     canUnload:dockedHere&&hold>0,
     canLoad:dockedHere&&!!bay?.ore&&hold<ship.capacity,
     canUndock:dockedHere,
     message:dockedHere?'Docked at home outpost. Transfer ore into the real local hangar.':
       dockState!==0?'Docking transition in progress or docked elsewhere.':
       !inRange?'Return to outpost '+site.e.id+'; '+Math.ceil(site.distance)+' mm away.':
       moving?'Outpost in range, but velocity must reach zero before docking.':
       'Outpost in range. Request docking to begin the physical transfer.'
   };
 }
 return {plan};
});
