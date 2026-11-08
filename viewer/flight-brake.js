/* PLAY-UI: voluntary momentum braking. Browser issues only native, bounded one-tick
 * thrust impulses from verified hull telemetry; it cannot set velocity or custody.
 */
(function(root,factory){'use strict';const api=factory();if(typeof module!=='undefined'&&module.exports)module.exports=api;root.GalaxyFlightBrake=api;})(typeof window!=='undefined'?window:globalThis,function(){
 'use strict';
 function plan(ship,opts={}){
   if(!ship||ship.entityType!==0||ship.owner!==7||
      ![ship.vx,ship.vy].every(Number.isSafeInteger))
     return {status:'invalid',reason:'No verified owned ship velocity'};
   if(opts.dockState!==0||opts.inTransition)return {status:'invalid',reason:'Ship unavailable for thrust'};
   if(ship.vx===0&&ship.vy===0)return {status:'stopped',reason:'Ship stationary'};
   const ax=ship.vx===0?0:-Math.sign(ship.vx), ay=ship.vy===0?0:-Math.sign(ship.vy);
   return {status:'thrust',command:{action:'thrust',extra:{x:ax,y:ay}},
      reason:'Counter-thrust from authoritative speed; momentum not changed in browser'};
 }
 return {plan};
});
