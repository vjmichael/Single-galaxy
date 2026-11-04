/* PLAY-13B.5: host-evidence-backed point-and-click scanner selection model.
 * This module does not produce scan results or send unimplemented commands.
 * The native host decides what is detectable and where a probe may go.
 */
(function(root,factory){
 const api=factory();
 if(typeof module==='object'&&module.exports)module.exports=api;
 if(root)root.GalaxyScannerSession=api;
})(typeof window!=='undefined'?window:undefined,function(){
 'use strict';
 const modes=Object.freeze(['environmental','celestial','anomaly']);
 const validId=n=>Number.isSafeInteger(n)&&n>0;
 function create(){
   let mode='anomaly',radiusMm=5000,contacts=[],selected=null;
   function current(){return contacts.find(x=>x.id===selected)||null;}
   return Object.freeze({
     selectMode(next){
       if(!modes.includes(next))throw Error('Unknown scanner mode');
       mode=next;selected=null;contacts=[];
     },
     setRadius(mm){
       if(!Number.isSafeInteger(mm)||mm<250||mm>2000000000)
         throw Error('Invalid scan radius');
       radiusMm=mm;
     },
     acceptContacts(records){
       if(!Array.isArray(records)||records.length>1000)throw Error('Invalid scan contacts');
       const seen=new Set(),next=[];
       for(const r of records){
         if(!r||!validId(r.id)||seen.has(r.id)||
            !Number.isSafeInteger(r.coarseX)||!Number.isSafeInteger(r.coarseY)||
            !Number.isSafeInteger(r.uncertaintyMm)||r.uncertaintyMm<0)
           throw Error('Invalid host signature');
         seen.add(r.id);
         // Copy only coarse public fields; ignore client-provided precise
         // coordinates, class names and arbitrary additional properties.
         next.push(Object.freeze({id:r.id,coarseX:r.coarseX,
           coarseY:r.coarseY,uncertaintyMm:r.uncertaintyMm}));
       }
       contacts=next;
       if(!current())selected=null;
     },
     lock(id){
       if(!validId(id)||!contacts.some(r=>r.id===id))
         throw Error('Cannot lock an undiscovered signature');
       selected=id;
     },
     clear(){selected=null;},
     state(){return {mode,radiusMm,selected,contacts:[...contacts]};},
     refinementRequest(probe){
       if(!validId(probe))throw Error('Select a deployed probe first');
       const contact=current();
       if(!contact)throw Error('Lock a host-observed signature first');
       // Build proposed action; host must authorize and derive all waypoints.
       return Object.freeze({action:'colonyProbeFocus',probe,
         signature:contact.id});
     }
   });
 }
 return Object.freeze({create,modes});
});
