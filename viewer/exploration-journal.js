/* PLAY-10: display of native saved discovery records. No discovery authority here. */
(function(root,factory){'use strict';const api=factory();if(typeof module!=='undefined'&&module.exports)module.exports=api;root.GalaxyExplorationJournal=api;})(typeof window!=='undefined'?window:globalThis,function(){
'use strict';
function entries(records,system=1,ship=null){
 const rows=[...(records?.values?.()||[])].filter(r=>r&&Number.isSafeInteger(r.site)&&r.site>0&&
   Number.isSafeInteger(r.system)&&r.system>0&&Number.isSafeInteger(r.firstTick)&&r.firstTick>0&&
   Number.isFinite(r.x)&&Number.isFinite(r.y));
 return rows.map(r=>({...r,local:r.system===system,
   distance:ship&&r.system===ship.system?Math.hypot(r.x-ship.x,r.y-ship.y):null}))
   .sort((a,b)=>a.firstTick-b.firstTick||a.site-b.site);
}
function describe(r){
 return '#'+r.site+' · '+(r.geological?'geological asteroid':'small asteroid')+
   ' · system '+r.system+' · mapped at '+Math.round(r.x).toLocaleString()+', '+
   Math.round(r.y).toLocaleString()+' mm · first discovered tick '+r.firstTick+
   (r.surveyTaxonomy>=1&&r.surveyTaxonomy<=3?
      ' · drone report: '+['Carbonaceous','Silicate','Metal-rich'][r.surveyTaxonomy-1]:'')+
   (r.distance!==null?' · '+Math.round(r.distance).toLocaleString()+' mm from ship':'');
}
function stats(rows){
 const local=rows.filter(r=>r.local);
 return {total:rows.length,local:local.length,geological:rows.filter(r=>r.geological).length,
   systems:new Set(rows.map(r=>r.system)).size};
}
return {entries,describe,stats};
});
