/* Interactive offline construction-layout UI. No network or trusted world authority. */
(function () {
'use strict';
const {Editor,WORKSHOP,edgeAt,edgeKey,endpoints,point}=window.GalaxyFactory;
const ui=id=>document.getElementById(id);
const canvas=ui('factoryCanvas'),holder=ui('canvasHolder'),ctx=canvas.getContext('2d');
const toolTitles={select:'Select / move',pan:'Pan view',base:'Base storage',storage:'Storage container',workshop:'Tier 1 workshop',route:'Automatic conveyor route',wall:'Snapping walls',door:'Door opening',floor:'Floor tiles',erase:'Erase design element'};
const descriptions={select:'Click equipment and drag to another free grid cell.',pan:'Drag to navigate; scroll to zoom.',base:'Click a grid cell to place base outpost storage.',storage:'Click to place a 4 m³ storage container.',workshop:'Click to place one 2 × 2 Tier 1 workshop.',route:'Click source equipment, then destination equipment.',wall:'Click or drag from one snapped grid vertex to another; corners connect automatically.',door:'Click a wall edge to create a snapped doorway.',floor:'Click to paint planning-floor tiles.',erase:'Click a device, conveyor, floor, or wall edge to remove it.'};
let editor=new Editor(),tool='select',hover=null,routeFrom=null,selected=null,drag=null,lastDraw=0;
const cam={x:5,y:3,zoom:42};
function resize(){const bounds=holder.getBoundingClientRect(),dpr=Math.max(1,Math.min(window.devicePixelRatio||1,2));canvas.width=Math.round(bounds.width*dpr);canvas.height=Math.round(bounds.height*dpr);ctx.setTransform(dpr,0,0,dpr,0,0);draw();}
function screen(wx,wy){return {x:(wx-cam.x)*cam.zoom+holder.clientWidth/2,y:(wy-cam.y)*cam.zoom+holder.clientHeight/2};}
function world(clientX,clientY){const b=canvas.getBoundingClientRect();return {x:(clientX-b.left-holder.clientWidth/2)/cam.zoom+cam.x,y:(clientY-b.top-holder.clientHeight/2)/cam.zoom+cam.y};}
function gridCell(p){return {x:Math.floor(p.x),y:Math.floor(p.y)};}
function vertex(p){return point(Math.round(p.x),Math.round(p.y));}
function message(text,isError=false){const el=ui('message');el.textContent=text;el.classList.toggle('error',isError);}
function pretty(id){return id===WORKSHOP?'Tier 1 workshop':id===1?'Base storage':`Storage #${id}`;}
function update(){const a=editor.audit();
  ui('metricDevices').textContent=a.devices;ui('metricLinks').textContent=a.conveyors;ui('metricSteps').textContent=a.jobs;ui('metricModules').textContent=a.modules;
  const info=ui('auditResult');info.classList.toggle('warn',!a.valid);info.textContent=a.valid?'Blueprint topology ready for native planner export.':a.errors.join(' · ');
  ui('planDetails').textContent=`Conveyor segments: ${a.segments}  ·  Walls: ${a.walls}  ·  Floors: ${a.floors}\nConstruction jobs are estimates, not yet executed.`;
  const d=editor.devices.find(x=>x.id===selected);ui('selectionDetails').textContent=d?`${pretty(d.id)} · position (${d.x}, ${d.y}) · ${d.w} × ${d.h} grid · ${d.kind==='workshop'?'Input aluminum; output frames, panels, scrap':'4 m³ storage'}`:'No equipment selected';
  ui('undo').disabled=!editor.history.length;ui('redo').disabled=!editor.future.length;
  draw();}
function draw(){if(!ctx||!canvas.width)return;const w=holder.clientWidth,h=holder.clientHeight;ctx.clearRect(0,0,w,h);ctx.fillStyle='#081421';ctx.fillRect(0,0,w,h);
  const xmin=Math.floor(cam.x-w/2/cam.zoom)-1,xmax=Math.ceil(cam.x+w/2/cam.zoom)+1,ymin=Math.floor(cam.y-h/2/cam.zoom)-1,ymax=Math.ceil(cam.y+h/2/cam.zoom)+1;
  ctx.lineWidth=1;ctx.strokeStyle='#213147';ctx.beginPath();for(let x=xmin;x<=xmax&&x<xmin+180;x++){const s=screen(x,0);ctx.moveTo(Math.round(s.x)+.5,0);ctx.lineTo(Math.round(s.x)+.5,h);}for(let y=ymin;y<=ymax&&y<ymin+180;y++){const s=screen(0,y);ctx.moveTo(0,Math.round(s.y)+.5);ctx.lineTo(w,Math.round(s.y)+.5);}ctx.stroke();
  if(xmin<=0&&xmax>=0){const a=screen(0,0);ctx.strokeStyle='#36556c';ctx.beginPath();ctx.moveTo(a.x,0);ctx.lineTo(a.x,h);ctx.stroke();}if(ymin<=0&&ymax>=0){const a=screen(0,0);ctx.strokeStyle='#36556c';ctx.beginPath();ctx.moveTo(0,a.y);ctx.lineTo(w,a.y);ctx.stroke();}
  for(const f of editor.floors){const p=screen(f.x,f.y);ctx.fillStyle='rgba(65,131,143,.17)';ctx.fillRect(p.x+1,p.y+1,cam.zoom-2,cam.zoom-2);}
  for(const r of editor.routes){ctx.strokeStyle='#50c4af';ctx.lineWidth=Math.max(3,cam.zoom*.21);ctx.lineCap='round';ctx.lineJoin='round';ctx.beginPath();r.path.forEach((p,i)=>{const s=screen(p.x+.5,p.y+.5);if(i)ctx.lineTo(s.x,s.y);else ctx.moveTo(s.x,s.y);});ctx.stroke();ctx.fillStyle='#c0fff0';ctx.font='11px system-ui';const mid=r.path[Math.floor(r.path.length/2)];if(mid&&cam.zoom>=26){const s=screen(mid.x+.5,mid.y+.5);ctx.fillText(`${r.rate}/tick`,s.x+5,s.y-10);}}
  for(const d of editor.devices){const a=screen(d.x,d.y),px=d.w*cam.zoom,py=d.h*cam.zoom;ctx.fillStyle=d.kind==='workshop'?'#735631':d.id===1?'#275878':'#25506d';ctx.strokeStyle=d.id===selected?'#fff1b5':d.kind==='workshop'?'#f6c97a':'#82cdeb';ctx.lineWidth=d.id===selected?3:2;ctx.fillRect(a.x+2,a.y+2,px-4,py-4);ctx.strokeRect(a.x+2,a.y+2,px-4,py-4);const at=screen(d.x+d.w/2,d.y+d.h/2);ctx.fillStyle='#f5f8fc';ctx.textAlign='center';ctx.textBaseline='middle';ctx.font=`${Math.max(11,Math.min(16,cam.zoom*.3))}px system-ui`;ctx.fillText(d.kind==='workshop'?'⚙':d.id===1?'▣':'▤',at.x,at.y-5);if(cam.zoom>25)ctx.fillText(d.kind==='workshop'?'Workshop':d.id===1?'Base':`#${d.id}`,at.x,at.y+12);}
  for(const e of editor.walls)drawEdge(e,e.type);
  if(drag?.kind==='wall'){
    const end=alignedVertex(drag.start,drag.current);
    const a=screen(drag.start.x,drag.start.y),b=screen(end.x,end.y);ctx.lineWidth=4;ctx.strokeStyle=drag.tool==='door'?'#eaa971':'#dceafa';ctx.globalAlpha=.58;ctx.setLineDash([8,5]);ctx.beginPath();ctx.moveTo(a.x,a.y);ctx.lineTo(b.x,b.y);ctx.stroke();ctx.setLineDash([]);ctx.globalAlpha=1;
  }
  if(hover&&!drag){
    if(['base','storage','workshop'].includes(tool)){const p=gridCell(hover),s=screen(p.x,p.y),k=tool==='workshop'?2:1;let valid=true;try{const copy=new Editor(editor.snapshot());copy.addDevice(tool,p.x,p.y);}catch{valid=false;}
      ctx.fillStyle=valid?'rgba(118,222,179,.23)':'rgba(245,109,103,.29)';ctx.strokeStyle=valid?'#a6f3c6':'#f7a9a4';ctx.lineWidth=2;ctx.fillRect(s.x+1,s.y+1,cam.zoom*k-2,cam.zoom*k-2);ctx.strokeRect(s.x+1,s.y+1,cam.zoom*k-2,cam.zoom*k-2);}
    if(['wall','door'].includes(tool)){const e=edgeAt(hover.x,hover.y);drawEdge({...e},tool,true);}
    if(tool==='floor'){const p=gridCell(hover),s=screen(p.x,p.y);ctx.fillStyle='rgba(106,224,206,.26)';ctx.fillRect(s.x+1,s.y+1,cam.zoom-2,cam.zoom-2);}
  }
  if(routeFrom!==null){const d=editor.devices.find(v=>v.id===routeFrom);if(d){const p=screen(d.x,d.y);ctx.strokeStyle='#e6ffd2';ctx.lineWidth=3;ctx.setLineDash([5,3]);ctx.strokeRect(p.x-3,p.y-3,d.w*cam.zoom+6,d.h*cam.zoom+6);ctx.setLineDash([]);}}
  ctx.textAlign='start';ctx.textBaseline='alphabetic';
}
function drawEdge(e,type,ghost=false){const [a,b]=endpoints(e),p=screen(a.x,a.y),q=screen(b.x,b.y);ctx.strokeStyle=type==='door'?'#eaa66e':ghost?'#99e6f8':'#e5edf7';ctx.lineWidth=type==='door'?Math.max(3,cam.zoom*.11):Math.max(3,cam.zoom*.16);ctx.lineCap='round';ctx.setLineDash(type==='door'?[Math.max(4,cam.zoom*.17),Math.max(3,cam.zoom*.12)]:[]);ctx.globalAlpha=ghost?.5:1;ctx.beginPath();ctx.moveTo(p.x,p.y);ctx.lineTo(q.x,q.y);ctx.stroke();ctx.setLineDash([]);ctx.globalAlpha=1;}
function alignedVertex(a,b){const dx=b.x-a.x,dy=b.y-a.y;return Math.abs(dx)>=Math.abs(dy)?point(b.x,a.y):point(a.x,b.y);}
function selectTool(next){tool=next;routeFrom=null;drag=null;for(const b of document.querySelectorAll('[data-tool]'))b.classList.toggle('active',b.dataset.tool===tool);ui('toolTitle').textContent=toolTitles[tool];ui('toolDescription').textContent=descriptions[tool];draw();}
function act(fn){try{fn();update();return true;}catch(error){message(error.message,true);draw();return false;}}
function click(p){const cell=gridCell(p),dev=editor.deviceAt(cell);
  if(['base','storage','workshop'].includes(tool))return act(()=>{editor.addDevice(tool,cell.x,cell.y);message('Ghost equipment placed. Exporting a blueprint does not spend construction materials.');});
  if(tool==='floor')return act(()=>{editor.setFloor(cell.x,cell.y);message('Floor tile added to the design overlay.');});
  if(tool==='route'){
    if(!dev){message('Select equipment as the source or destination, not an empty tile.',true);return;}
    if(routeFrom===null){routeFrom=dev.id;selected=dev.id;message(`${pretty(dev.id)} selected. Click destination equipment.`);update();return;}
    const from=routeFrom;routeFrom=null;
    if(from===dev.id){message('Select a different destination.',true);return;}
    return act(()=>{const rate=Number(ui('routeRate').value),item=ui('routeItem').value;const id=editor.autoRoute(from,dev.id,item,rate);message(`Conveyor #${id} planned. No materials spent.`);});
  }
  if(tool==='select'){selected=dev?.id??null;update();return;}
  if(tool==='erase'){
    const e=edgeAt(p.x,p.y),find=editor.walls.find(w=>edgeKey(w)===edgeKey(e));
    if(find&&Math.min(Math.abs(p.x-Math.round(p.x)),Math.abs(p.y-Math.round(p.y)))<.22){act(()=>{editor.setEdge(e,'erase');message('Wall or door segment removed.');});return;}
    act(()=>message(editor.eraseCell(cell.x,cell.y)));return;
  }
  if(tool==='wall'||tool==='door')return act(()=>{editor.setEdge(edgeAt(p.x,p.y),tool);message('Snapped wall/door segment added to design overlay.');});
}
canvas.addEventListener('pointerdown',e=>{
  if(e.button!==0&&e.button!==1&&e.button!==2)return;
  canvas.setPointerCapture(e.pointerId);const p=world(e.clientX,e.clientY);hover=p;
  if(tool==='pan'||e.button===1||e.button===2||e.shiftKey){drag={kind:'pan',startX:e.clientX,startY:e.clientY,cx:cam.x,cy:cam.y};return;}
  if(tool==='wall'||tool==='door'){drag={kind:'wall',start:vertex(p),current:vertex(p),tool};return;}
  if(tool==='select'){
    const d=editor.deviceAt(gridCell(p));if(d){selected=d.id;drag={kind:'move',id:d.id,start:point(d.x,d.y),pointer:gridCell(p),current:gridCell(p)};update();return;}
  }
  drag={kind:'click',p};
});
canvas.addEventListener('pointermove',e=>{const p=world(e.clientX,e.clientY);hover=p;ui('pointerInfo').textContent=`(${Math.floor(p.x)}, ${Math.floor(p.y)}) · ${Math.round(cam.zoom/42*100)}%`;
  if(drag?.kind==='pan'){cam.x=drag.cx-(e.clientX-drag.startX)/cam.zoom;cam.y=drag.cy-(e.clientY-drag.startY)/cam.zoom;}
  if(drag?.kind==='wall')drag.current=vertex(p);
  if(drag?.kind==='move')drag.current=gridCell(p);
  draw();
});
canvas.addEventListener('pointerup',e=>{
  if(!drag)return;const d=drag;drag=null;
  if(d.kind==='wall'){
    const end=alignedVertex(d.start,vertex(world(e.clientX,e.clientY)));
    if(end.x===d.start.x&&end.y===d.start.y)click(world(e.clientX,e.clientY));
    else act(()=>{editor.setEdgeRun(d.start,end,d.tool);message(`${d.tool==='wall'?'Wall':'Door'} edges snapped to shared grid vertices.`);});
  }else if(d.kind==='move'){
    const next=gridCell(world(e.clientX,e.clientY));const x=d.start.x+next.x-d.pointer.x,y=d.start.y+next.y-d.pointer.y;
    if(x!==d.start.x||y!==d.start.y)act(()=>{editor.moveDevice(d.id,x,y);message(`${pretty(d.id)} moved to (${x}, ${y}).`);});else{selected=d.id;update();}
  }else if(d.kind==='click')click(world(e.clientX,e.clientY));draw();
});
canvas.addEventListener('pointercancel',()=>{drag=null;draw();});
canvas.addEventListener('contextmenu',e=>e.preventDefault());
canvas.addEventListener('wheel',e=>{e.preventDefault();const before=world(e.clientX,e.clientY),next=Math.min(110,Math.max(16,cam.zoom*(e.deltaY>0?.9:1.1)));cam.zoom=next;const after=world(e.clientX,e.clientY);cam.x+=before.x-after.x;cam.y+=before.y-after.y;draw();},{passive:false});
document.addEventListener('keydown',e=>{
  const typing=['INPUT','SELECT','TEXTAREA'].includes(document.activeElement?.tagName);if(typing)return;
  if((e.ctrlKey||e.metaKey)&&e.key.toLowerCase()==='z'){e.preventDefault();if(e.shiftKey)editor.redo();else editor.undo();update();return;}
  if((e.ctrlKey||e.metaKey)&&e.key.toLowerCase()==='y'){e.preventDefault();editor.redo();update();return;}
  if(e.key==='Escape'){routeFrom=null;drag=null;message('Current placement cancelled.');draw();return;}
  if(e.key.toLowerCase()==='v')selectTool('select');if(e.key.toLowerCase()==='h')selectTool('pan');
  if(e.key==='Delete'&&selected!==null){const d=editor.devices.find(v=>v.id===selected);if(d)act(()=>{editor.eraseCell(d.x,d.y);selected=null;message('Equipment removed from blueprint.');});}
});
for(const b of document.querySelectorAll('[data-tool]'))b.addEventListener('click',()=>selectTool(b.dataset.tool));
ui('undo').onclick=()=>{editor.undo();update();};ui('redo').onclick=()=>{editor.redo();update();};
ui('clear').onclick=()=>{if(window.confirm('Clear all equipment, conveyors and design overlays?')){editor.clear();selected=null;routeFrom=null;update();message('Plan cleared.');}};
function sample(){const e=new Editor();e.addDevice('base',0,0);e.addDevice('storage',4,0);e.addDevice('workshop',8,0);e.autoRoute(1,2,'good/aluminum',7);e.autoRoute(2,WORKSHOP,'good/aluminum',5);
  // A row of snap-linked walls above the production path, plus a doorway.
  e.setEdgeRun({x:-2,y:-3},{x:12,y:-3},'wall');e.setEdgeRun({x:-2,y:-3},{x:-2,y:5},'wall');e.setEdgeRun({x:12,y:-3},{x:12,y:5},'wall');e.setEdgeRun({x:-2,y:5},{x:12,y:5},'wall');e.setEdge({axis:'h',x:2,y:5},'door');
  for(let x=-1;x<=11;x++)for(let y=-2;y<=4;y++)e.setFloor(x,y);
  return e;
}
ui('example').onclick=()=>{editor=sample();selected=null;routeFrom=null;cam.x=5;cam.y=1;update();message('Sample loaded: 3 devices, 2 automatic routes, snapped walls, a door and floor tiles.');};
function download(name,type,data){const blob=new Blob([data],{type});const url=URL.createObjectURL(blob);const link=document.createElement('a');link.href=url;link.download=name;document.body.appendChild(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(url),0);}
ui('saveDesign').onclick=()=>act(()=>{download('single-galaxy-factory.sgfactory','application/json',editor.exportDesign());message('Saved editable .sgfactory: includes walls, doors, floors, machines and conveyors.');});
ui('exportNative').onclick=()=>act(()=>{const a=editor.audit();if(!a.valid)throw Error(a.errors.join('; '));download('single-galaxy-factory.sglayout','text/plain',editor.canonical());message('Exported canonical .sglayout for the native construction planner. Walls/floors are not included.');});
ui('auditButton').onclick=()=>{const a=editor.audit();update();message(a.valid?`Blueprint ready: ${a.jobs} construction steps / ${a.modules} modules. No construction executed.`:a.errors.join('; '),!a.valid);};
ui('openDesign').onclick=()=>ui('loadFile').click();
ui('loadFile').addEventListener('change',async e=>{const f=e.target.files[0];if(!f)return;try{const text=await f.text();const native=f.name.toLowerCase().endsWith('.sglayout');if(!native&&!f.name.toLowerCase().endsWith('.sgfactory')&&!f.name.toLowerCase().endsWith('.json'))throw Error('Open an editable .sgfactory or native .sglayout file.');const next=native?new Editor().loadCanonical(text):new Editor(text);editor=next;selected=null;routeFrom=null;update();message(`${f.name} loaded and checked. The design does not modify a live world.`);}catch(err){message(err.message,true);}finally{e.target.value='';}});
if(window.ResizeObserver)new ResizeObserver(resize).observe(holder);else window.addEventListener('resize',resize);
resize();update();
})();
