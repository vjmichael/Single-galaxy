/* B6.4 original regional starmap. The display graph is guidance, not authority:
   requests are independently validated against C++'s committed physical gates. */
(function(root){'use strict';
 const specs=[
  [1,'Haven',38,113,'NPC'],[2,'Sable Gap',92,185,'Frontier'],
  [3,'Lumenford',91,37,'NPC'],[4,'Glass Meridian',160,76,'NPC'],
  [5,'Driftwater',236,35,'NPC'],[6,'Cinder Cut',180,183,'Frontier'],
  [7,'Far Lantern',251,124,'Player']
 ];
 const links=[[1,2],[1,3],[2,6],[6,7],[3,4],[4,5],[5,7],[2,4],[4,6]];
 const el=typeof document!=='undefined'?document.getElementById('regionMap'):null;
 let system=1,route=[],leg=0;
 const ns='http://www.w3.org/2000/svg';
 const xy=id=>specs.find(s=>s[0]===id);
 function shape(type,attrs,txt){
  const n=document.createElementNS(ns,type);
  for(const [k,v] of Object.entries(attrs))n.setAttribute(k,String(v));
  if(txt!==undefined)n.textContent=txt;
  return n;
 }
 function update(){
  if(!el)return;
  el.replaceChildren();
  for(const [a,b] of links){
   const u=xy(a),v=xy(b);
   const planned=route.some((n,i)=>i>0&&((route[i-1]===a&&n===b)||(route[i-1]===b&&n===a)));
   el.appendChild(shape('line',{x1:u[2],y1:u[3],x2:v[2],y2:v[3],stroke:planned?'#e5b560':'#3b526b','stroke-width':planned?3:1.4,'stroke-linecap':'round'}));
  }
  for(const [id,name,x,y,sovereignty] of specs){
   const current=id===system,next=id===leg;
   const tone=current?'#6fe1fc':next?'#f4ce79':sovereignty==='NPC'?'#82acdb':'#b3a0d4';
   el.appendChild(shape('circle',{cx:x,cy:y,r:current?10:next?9:7,fill:'#07111f',stroke:tone,'stroke-width':current?3:1.7}));
   el.appendChild(shape('text',{x,y:y+2.8,fill:tone,'text-anchor':'middle','font-size':8,'font-family':'system-ui','font-weight':700},String(id)));
   const align=x>210?'end':x<60?'start':'middle';
   const labelX=align==='end'?x-12:align==='start'?x+12:x;
   const labelY=(id===2||id===6)?y+22:y-14;
   el.appendChild(shape('text',{x:labelX,y:labelY,fill:'#c7d7e9','text-anchor':align,'font-size':9,'font-family':'system-ui'},name));
  }
 }
 const api={
  onEvent(event){
   if(event.type==='SYSTEM'){system=event.system;leg=0;if(!route.includes(system))route=[];}
   if(event.type==='ROUTE'||event.type==='REROUTE'){route=event.path;}
   if(event.type==='ROUTE_NONE'){route=[];leg=0;}
   if(event.type==='LEG'){leg=event.next;}
   update();
  },
  reset(){system=1;route=[];leg=0;update();},
  nodes:specs.map(s=>({id:s[0],name:s[1],security:s[4]})),links:links.map(x=>x.slice())
 };
 root.GalaxyRegionView=api;update();
})(typeof window!=='undefined'?window:globalThis);
