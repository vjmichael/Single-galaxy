/* PLAY-07: bounded non-authoritative assisted flight.
 * Generates ordinary one-tick world-axis THRUST commands. No teleport,
 * fictional physics, native authorization or background command queue.
 */
(function(root,factory){'use strict';const api=factory();if(typeof module!=='undefined'&&module.exports)module.exports=api;root.GalaxyTravelAssist=api;})(typeof window!=='undefined'?window:globalThis,function(){
 'use strict';
 const ACCELERATION=250;
 const MAX_TICKS=1800;
 function axis(pos,velocity,dest,stopRadius){
   if(![pos,velocity,dest,stopRadius].every(Number.isFinite)||stopRadius<0)return null;
   const remaining=dest-pos, distance=Math.abs(remaining);
   if(distance<=stopRadius&&velocity===0)return 0;
   if(velocity!==0){
     const slowing=(Math.abs(velocity)*(Math.abs(velocity)+ACCELERATION))/(2*ACCELERATION);
     // Decelerate if wrong-way, or too near to stop at the target.
     if(Math.sign(velocity)!==Math.sign(remaining)||distance<=slowing+stopRadius+ACCELERATION)
       return -Math.sign(velocity);
   }
   if(distance<=stopRadius)return -Math.sign(velocity);
   return Math.sign(remaining);
 }
 function plan(ship,destination,opts={}){
   if(!ship||!destination||![ship.x,ship.y,ship.vx,ship.vy,destination.x,destination.y].every(Number.isFinite))
      return {status:'invalid',reason:'Missing authoritative position or velocity.'};
   if(ship.system!==destination.system)return {status:'invalid',reason:'Destination is in another system.'};
   const dockState=opts.dockState??0;
   if(dockState!==0||opts.jumpActive)return {status:'invalid',reason:'Ship is docked or in a jump transition.'};
   // Target center means the arrival region can be used for either asteroid mining
   // or an owned outpost; native range remains independent and authoritative.
   const tolerance=opts.tolerance??500;
   if(!Number.isFinite(tolerance)||tolerance<0||tolerance>2500)
     return {status:'invalid',reason:'Invalid arrival tolerance.'};
   const dx=destination.x-ship.x,dy=destination.y-ship.y;
   const distance=Math.hypot(dx,dy);
   if(distance<=tolerance && ship.vx===0 && ship.vy===0)
     return {status:'arrived',distance,reason:'Within arrival area and stationary. Native range checks still apply.'};
   const x=axis(ship.x,ship.vx,destination.x,tolerance/Math.SQRT2);
   const y=axis(ship.y,ship.vy,destination.y,tolerance/Math.SQRT2);
   if(x===null||y===null)return {status:'invalid',reason:'Invalid thrust projection.'};
   return {status:'thrust',distance,command:{action:'thrust',extra:{x,y}},
     reason:'Assisted flight: applying one native acceleration impulse.'};
 }
 function target(state,kind,id,outposts){
   if(!state?.entities||!Number.isSafeInteger(id)||id<=0)return null;
   const e=state.entities.get(id);
   if(!e)return null;
   if(kind==='rock'&&e.entityType===1&&e.ore>0&&outposts!==null)return e;
   if(kind==='home'&&e.entityType===2&&e.owner===7&&outposts?.has(id))return e;
   return null;
 }
 function statusText(status,description){
   if(status==='idle')return 'Manual flight. Select an asteroid or your home outpost to engage travel assist.';
   if(status==='arrived')return 'Arrived and stopped near '+description+'. Dock or mine manually.';
   if(status==='cancelled')return 'Travel assist cancelled. Your ship retains real momentum.';
   if(status==='paused')return 'Travel assist stopped: native world or control input changed.';
   if(status==='travel')return 'Assisting toward '+description+' using native thrust. Press Cancel or steer manually to disengage.';
   return 'Travel assist unavailable.';
 }
 return {ACCELERATION,MAX_TICKS,axis,plan,target,statusText};
});
