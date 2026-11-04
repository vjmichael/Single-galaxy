/* PLAY-05 — observational, per-live-session mining expedition feedback.
 * The authoritative native world owns cargo, coordinates, docking and events.
 * No local state can grant or commit items. Replay/reconnect resets the tracker.
 */
(function(root,factory){'use strict';const api=factory();if(typeof module!=='undefined'&&module.exports)module.exports=api;root.GalaxyExpeditionFeedback=api;})(typeof window!=='undefined'?window:globalThis,function(){
'use strict';
const OUTCOMES=Object.freeze({
 APPLIED:'Native command applied. Check updated world inventory for the actual effect.',
 OUT_OF_RANGE:'Target is out of mining or docking range. Pilot closer.',
 CARGO_FULL:'Cargo hold full. Return to an outpost or process bulk ore.',
 NO_RESOURCES:'This target or inventory has no available bulk ore.',
 SHIP_MOVING:'Ship is still moving. Apply counter-thrust until velocity reaches zero.',
 NOT_IN_SPACE:'You must be in open space to do this.',
 NOT_DOCKED:'Dock first, then transfer ore.',
 IN_TRANSITION:'Wait until docking or another transition finishes.',
 INVALID_TARGET:'Target is missing or unavailable. Refresh the scan or choose another outpost.',
 INSUFFICIENT_CREW:'Not enough available crew for this action.',
 UNAUTHORIZED:'Action refused: you do not own or control this asset.',
 INVALID_ARGUMENT:'The command arguments were refused.',
 HANGAR_FULL:'The destination hangar is full.',
 WRONG_SYSTEM:'Target is in another system.'
});
function diagnostic(outcome,transferred){
 const word=typeof outcome==='string'?outcome.toUpperCase():'';
 const msg=Object.prototype.hasOwnProperty.call(OUTCOMES,word)?OUTCOMES[word]:'Native command reported '+word+'.';
 const qty=Number.isSafeInteger(transferred)&&transferred>=0?' · reported transfer '+transferred:''; 
 return msg+qty;
}
function target(ship,rocks,selectedId=null){
 if(!ship||!Array.isArray(rocks)||![ship.x,ship.y].every(Number.isFinite))return null;
 const candidates=rocks.filter(r=>r?.entityType===1&&r.system===ship.system&&r.ore>0&&
   [r.x,r.y].every(Number.isFinite)).map(r=>({id:r.id,ore:r.ore,
   distance:Math.hypot(r.x-ship.x,r.y-ship.y)}));
 candidates.sort((a,b)=>a.distance-b.distance||a.id-b.id);
 return candidates.find(r=>r.id===selectedId)||candidates[0]||null;
}
function initial(){return {last:null,mined:0,delivered:0,phase:'awaiting',success:false,home:null};}
function advance(prev,observation){
 if(!observation||!Number.isSafeInteger(observation.hold)||observation.hold<0||
   !Number.isSafeInteger(observation.bay)||observation.bay<0||
   !Number.isSafeInteger(observation.tick)||observation.tick<0||
   typeof observation.docked!=='boolean'||!Number.isSafeInteger(observation.home)||observation.home<=0)
   return prev;
 const current={tick:observation.tick,hold:observation.hold,bay:observation.bay,
   docked:observation.docked,home:observation.home};
 if(!prev.last||prev.home!==current.home||current.tick<prev.last.tick)
   return {...initial(),last:current,home:current.home,phase:'mine'};
 if(current.tick===prev.last.tick)return prev;
 // Only an increase in bulk ship ore while *undocked* constitutes observed mining.
 const gain=!current.docked&&current.hold>prev.last.hold?
   current.hold-prev.last.hold:0;
 const mined=prev.mined+gain;
 // A successful delivery requires both observed mining in THIS session and
 // a true home-bay increase while ship is docked (not a mere UI click).
 const supplied=current.docked&&prev.mined>prev.delivered&&current.bay>prev.last.bay?
   Math.min(current.bay-prev.last.bay,prev.mined-prev.delivered):0;
 const delivered=prev.delivered+supplied;
 const success=delivered>0;
 const phase=success?'delivered':current.docked?'unload':
   mined>0?'return':'mine';
 return {last:current,home:current.home,mined,delivered,success,phase};
}
function status(view){
 if(!view.last)return 'Awaiting current ship and outpost inventory. No progress assumed.';
 if(view.success)return 'Expedition complete: '+view.delivered+' t of this session’s newly mined bulk ore observed deposited at home.';
 if(view.phase==='unload')return 'Docked at home. Use Unload bulk ore to finish your expedition.';
 if(view.phase==='return')return view.mined+' t of new ore mined in this session. Navigate home and dock to deliver it.';
 return 'Mine bulk ore from an in-range asteroid to begin your next expedition.';
}
return {diagnostic,target,initial,advance,status};
});
