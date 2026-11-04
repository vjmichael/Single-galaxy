/* PLAY-06: view-only camera and screen targeting, no native world authority. */
(function(root,factory){'use strict';const api=factory();if(typeof module!=='undefined'&&module.exports)module.exports=api;root.GalaxyFlightCamera=api;})(typeof window!=='undefined'?window:globalThis,function(){
 'use strict';
 const MIN_ZOOM=0.35, MAX_ZOOM=8;
 function clampZoom(x){return Number.isFinite(x)?Math.max(MIN_ZOOM,Math.min(MAX_ZOOM,x)):1;}
 function viewport(focus,zoom=1,span=100000){
   if(!focus||![focus.x,focus.y].every(Number.isFinite))return null;
   const width=Math.max(4500,Math.min(2e9,span))/clampZoom(zoom);
   return {left:focus.x-width/2,right:focus.x+width/2,
     top:focus.y-width/2,bottom:focus.y+width/2};
 }
 function fit(entities){
   const points=[...entities].filter(e=>e&&Number.isFinite(e.x)&&Number.isFinite(e.y));
   if(!points.length)return {left:-5000,right:5000,top:-5000,bottom:5000};
   const minX=Math.min(...points.map(e=>e.x)),maxX=Math.max(...points.map(e=>e.x));
   const minY=Math.min(...points.map(e=>e.y)),maxY=Math.max(...points.map(e=>e.y));
   const span=Math.max(maxX-minX,maxY-minY,4500)*1.4;
   return viewport({x:(minX+maxX)/2,y:(minY+maxY)/2},1,span);
 }
 function scale(bounds,width,height){
   if(!bounds||!Number.isFinite(width)||!Number.isFinite(height)||width<=100||height<=100)return null;
   const w=bounds.right-bounds.left,h=bounds.bottom-bounds.top;
   if(!(w>0&&h>0))return null;
   const k=Math.min((width-100)/w,(height-100)/h);
   return {k,ox:width/2-(bounds.left+bounds.right)*k/2,
     oy:height/2-(bounds.top+bounds.bottom)*k/2};
 }
 function toScreen(p,bounds,width,height){
   const t=scale(bounds,width,height);if(!t||!p)return null;
   return {x:t.ox+p.x*t.k,y:t.oy+p.y*t.k};
 }
 function pickRock(rocks,x,y,bounds,width,height,radius=22){
   if(!Number.isFinite(x)||!Number.isFinite(y)||!(radius>0))return null;
   let selected=null,best=radius*radius;
   for(const rock of rocks||[]){
     if(!rock||rock.entityType!==1||!(rock.ore>0)||
       !Number.isFinite(rock.x)||!Number.isFinite(rock.y))continue;
     const screen=toScreen(rock,bounds,width,height);if(!screen)continue;
     const d=(screen.x-x)**2+(screen.y-y)**2;
     if(d>best)continue;
     if(!selected||d<best||(d===best&&rock.id<selected.id)){
       selected=rock;best=d;
     }
   }
   return selected?.id??null;
 }
 return {clampZoom,viewport,fit,toScreen,pickRock,MIN_ZOOM,MAX_ZOOM};
});
