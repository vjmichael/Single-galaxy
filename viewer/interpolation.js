/* Visual-only interpolation. Never used in gameplay, collision, or hashes. */
(function(root,factory){const api=factory();if(typeof module!=='undefined'&&module.exports)module.exports=api;root.GalaxyInterpolation=api;})(typeof window!=='undefined'?window:globalThis,function(){
'use strict';
function clamp(x){return Math.max(0,Math.min(1,Number.isFinite(x)?x:0));}
function interpolatePositions(previous,current,progress){
  if(!current)return null;
  if(!previous||previous.stream!==current.stream||previous.system!==current.system)return current;
  const a=clamp(progress),entities=new Map();
  for(const [id,e] of current.entities){
    const prior=previous.entities.get(id);
    if(!prior){entities.set(id,e);continue;}
    entities.set(id,{...e,x:prior.x+(e.x-prior.x)*a,y:prior.y+(e.y-prior.y)*a});
  }
  return {...current,entities};
}
return {interpolatePositions};
});
