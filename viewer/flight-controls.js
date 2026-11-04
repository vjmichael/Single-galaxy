/* PLAY-03: optional guide preference and safe local flight-input mapping.
 * No game authority or physics lives in this browser helper.
 */
(function(root,factory){'use strict';const api=factory();if(typeof module!=='undefined'&&module.exports)module.exports=api;root.GalaxyFlightControls=api;})(typeof window!=='undefined'?window:globalThis,function(){
 'use strict';
 const KEYS=Object.freeze({ArrowLeft:[-1,0],a:[-1,0],A:[-1,0],
   ArrowRight:[1,0],d:[1,0],D:[1,0],ArrowUp:[0,-1],w:[0,-1],W:[0,-1],
   ArrowDown:[0,1],s:[0,1],S:[0,1], ' ':[0,0]});
 const YAW_KEYS=Object.freeze({q:-15,Q:-15,e:15,E:15});
 const GUIDE_KEY='single-galaxy:skip-opening-guide:v1';
 function flightInput(key,editable=false,repeat=false,alt=false,ctrl=false,meta=false){
   if(editable||repeat||alt||ctrl||meta)return null;
   if(Object.prototype.hasOwnProperty.call(YAW_KEYS,key))return {action:'yaw',extra:{delta:YAW_KEYS[key]}};
   return !Object.prototype.hasOwnProperty.call(KEYS,key)?
      null:{action:'thrust',extra:{x:KEYS[key][0],y:KEYS[key][1]}};
 }
 function guideSkipped(storage){
   try{return storage?.getItem(GUIDE_KEY)==='yes';}catch{return false;}
 }
 function setGuideSkipped(storage,skip){
   try{if(skip)storage?.setItem(GUIDE_KEY,'yes');else storage?.removeItem(GUIDE_KEY);}catch{/* Private browsing can deny storage. */}
 }
 function rockRange(ship,rock){
   if(!ship||!rock||![ship.x,ship.y,rock.x,rock.y].every(Number.isFinite))return null;
   return Math.hypot(ship.x-rock.x,ship.y-rock.y);
 }
 return {flightInput,guideSkipped,setGuideSkipped,rockRange};
});
