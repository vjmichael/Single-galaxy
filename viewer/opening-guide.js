/* PLAY-02: read-only guide derived exclusively from committed colony STATUS.
 * Buttons send existing host actions. A queued HTTP response never advances a step.
 */
(function(root,factory){'use strict';const api=factory();if(typeof module!=='undefined'&&module.exports)module.exports=api;root.GalaxyOpeningGuide=api;})(typeof window!=='undefined'?window:globalThis,function(){
 'use strict';
 const actions=[
   {id:'colonyScan',title:'Survey nearby asteroids',detail:'Find a real mineable field. Scan does not create resources.'},
   {id:'colonyMine',title:'Mine 8 tonnes of ore',detail:'Approach an asteroid within mining range, then extract ore.'},
   {id:'colonyRefine',title:'Refine 8 tonnes',detail:'Convert mined cargo into onboard refined material.'},
   {id:'colonyFabricate',title:'Fabricate 4 modules',detail:'Manufacture construction modules from your refined material.'},
   {id:'colonyBuild',title:'Construct your first outpost',detail:'Use four modules and your actual crew to build a persistent outpost.'}
 ];
 function progress(s,scanned=false){
   if(!s || !['crew','raw','refined','modules','outposts'].every(k=>Number.isSafeInteger(s[k])&&s[k]>=0))
     return {stage:-1,done:false,title:'Waiting for verified colony status',detail:'Connecting to the authoritative world. No progress is assumed.',action:null};
   if(s.outposts>=1)return {stage:5,done:true,title:'First outpost established',detail:'Construction is committed to the world. Your next goal is to equip and supply the settlement.',action:null};
   // Current stores are authoritative. No claim about an earlier transaction
   // can be inferred from a lost resource or speculative client command.
   if(s.modules>=4)return {stage:4,done:false,...actions[4],action:actions[4].id};
   if(s.refined>=8)return {stage:3,done:false,...actions[3],action:actions[3].id};
   if(s.raw>=8)return {stage:2,done:false,...actions[2],action:actions[2].id};
   if(scanned)return {stage:1,done:false,...actions[1],action:actions[1].id};
   return {stage:0,done:false,...actions[0],action:actions[0].id};
 }
 return {progress};
});
