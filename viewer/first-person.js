/* FP3D-01: optional read-only, first-person ship camera over verified SGVIEW.
 * The engine remains the only authority for movement, custody and collisions.
 * All geometry is untextured proxy artwork; SGVIEW v1 supplies XY, not Z.
 */
(function(root,factory){
  'use strict';
  const api=factory();
  if(typeof module!=='undefined'&&module.exports)module.exports=api;
  if(root)root.GalaxyFirstPerson=api;
})(typeof window!=='undefined'?window:globalThis,function(){
'use strict';
const OWNER=7, MAX_DISTANCE_MM=2_000_000, MM_PER_UNIT=1_000;
const COLORS={0:[.12,.83,.92],1:[.75,.57,.40],2:[.64,.49,.94],3:[.34,.89,.61],4:[.91,.49,.29]};
function safeObject(e){
 return !!e&&Number.isSafeInteger(e.id)&&e.id>0&&Number.isSafeInteger(e.x)&&
   Number.isSafeInteger(e.y)&&Number.isSafeInteger(e.system)&&
   Number.isInteger(e.entityType)&&e.entityType>=0&&e.entityType<=4;
}
function project(frame,{activeId,homeId,heading,headingShipId}={}){
 if(!frame||!frame.entities||!(frame.entities instanceof Map))
   return {status:'no-frame',objects:[]};
 const values=[...frame.entities.values()];
 const valid=e=>safeObject(e)&&e.entityType===0&&e.owner===OWNER&&e.system===frame.system;
 let ship=null;
 if(Number.isSafeInteger(activeId)&&activeId>0)ship=frame.entities.get(activeId);
 else if(Number.isSafeInteger(homeId)&&homeId>0)ship=frame.entities.get(homeId);
 else ship=values.find(valid);
 if(!valid(ship))return {status:'no-owned-ship',objects:[]};
 if(!Number.isSafeInteger(heading)||heading<0||heading>=360||headingShipId!==ship.id)
   return {status:'no-native-heading',shipId:ship.id,objects:[]};
 const objects=[];
 for(const e of values){
   if(!safeObject(e)||e.id===ship.id||e.system!==frame.system)continue;
   // Native masking must not be reversed by presentation or pilot selection.
   if(e.entityType===0&&e.owner===0&&e.cargo!==0)continue;
   const dx=e.x-ship.x,dz=e.y-ship.y;
   if(!Number.isSafeInteger(dx)||!Number.isSafeInteger(dz)||
       Math.hypot(dx,dz)>MAX_DISTANCE_MM)continue;
   const nominal=e.entityType===1?Math.max(1,Math.min(14,Math.cbrt(Math.max(0,e.ore||0))/7)):
                 e.entityType===2?10:e.entityType===3?6:e.entityType===4?2:3;
   objects.push({id:e.id,kind:e.entityType,x:dx/MM_PER_UNIT,z:dz/MM_PER_UNIT,
     size:nominal,color:COLORS[e.entityType]});
 }
 objects.sort((a,b)=>a.id-b.id);
 return {status:'ready',shipId:ship.id,tick:frame.tick,system:frame.system,
   headingDegrees:heading,objects};
}
function triangles(faces){
 const result=[];
 for(const face of faces){
   const [a,b,c]=face;
   const u=[b[0]-a[0],b[1]-a[1],b[2]-a[2]],v=[c[0]-a[0],c[1]-a[1],c[2]-a[2]];
   const n=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]];
   const len=Math.hypot(...n)||1;
   for(const pt of face)result.push(...pt,...n.map(x=>x/len));
 }
 return new Float32Array(result);
}
const CUBE=triangles((()=>{
 const p=[[-1,-1,-1],[1,-1,-1],[1,1,-1],[-1,1,-1],[-1,-1,1],[1,-1,1],[1,1,1],[-1,1,1]];
 return [[0,1,2],[0,2,3],[4,7,6],[4,6,5],[0,4,5],[0,5,1],[1,5,6],[1,6,2],
 [2,6,7],[2,7,3],[3,7,4],[3,4,0]].map(f=>f.map(i=>p[i]));
})());
const ROCK=triangles((()=>{
 const p=[[0,1.4,0],[0,-1.2,0],[1,0,0],[0,0,1.1],[-1,0,0],[0,0,-1.1]];
 return [[0,2,3],[0,3,4],[0,4,5],[0,5,2],[1,3,2],[1,4,3],[1,5,4],[1,2,5]].map(f=>f.map(i=>p[i]));
})());
const SHIP=triangles((()=>{
 const p=[[2,0,0],[-1.1,.45,-.9],[-1.1,.45,.9],[-1.1,-.4,0]];
 return [[0,1,2],[0,2,3],[0,3,1],[1,3,2]].map(f=>f.map(i=>p[i]));
})());
const VS=`#version 300 es
in vec3 aPosition;
in vec3 aNormal;
uniform vec3 uOffset;
uniform vec3 uScale;
uniform float uYaw;
uniform float uAspect;
uniform float uFocal;
out float vLight;
void main(){
 vec3 p=aPosition*uScale+uOffset;
 float c=cos(uYaw),s=sin(uYaw);
 float forward=p.x*c+p.z*s;
 float right=-p.x*s+p.z*c;
 float n=0.05,f=5000.0;
 gl_Position=vec4(right*uFocal/uAspect,(p.y-1.7)*uFocal,
   (f+n)/(f-n)*forward-(2.0*f*n)/(f-n),forward);
 vLight=max(0.28,0.48+dot(normalize(aNormal),normalize(vec3(0.3,0.8,0.4)))*0.42);
}`;
const FS=`#version 300 es
precision mediump float;
in float vLight;
uniform vec3 uColor;
out vec4 fragColor;
void main(){fragColor=vec4(uColor*vLight,1.0);}`;
function shader(gl,type,text){const s=gl.createShader(type);gl.shaderSource(s,text);gl.compileShader(s);
 if(!gl.getShaderParameter(s,gl.COMPILE_STATUS))throw new Error(gl.getShaderInfoLog(s)||'Shader failed');return s;}
function createRenderer(canvas){
 const gl=canvas&&canvas.getContext&&canvas.getContext('webgl2',{antialias:true,alpha:false});
 if(!gl)throw new Error('WebGL2 unavailable; 2D map remains available');
 const program=gl.createProgram();const vert=shader(gl,gl.VERTEX_SHADER,VS),frag=shader(gl,gl.FRAGMENT_SHADER,FS);
 gl.attachShader(program,vert);gl.attachShader(program,frag);gl.linkProgram(program);
 if(!gl.getProgramParameter(program,gl.LINK_STATUS))throw new Error(gl.getProgramInfoLog(program)||'Shader link failed');
 gl.useProgram(program);gl.enable(gl.DEPTH_TEST);
 const loc={p:gl.getAttribLocation(program,'aPosition'),n:gl.getAttribLocation(program,'aNormal'),
   offset:gl.getUniformLocation(program,'uOffset'),scale:gl.getUniformLocation(program,'uScale'),
   yaw:gl.getUniformLocation(program,'uYaw'),aspect:gl.getUniformLocation(program,'uAspect'),
   focal:gl.getUniformLocation(program,'uFocal'),color:gl.getUniformLocation(program,'uColor')};
 const buffers=new Map();
 for(const [kind,shape] of [[0,SHIP],[1,ROCK],[2,CUBE],[3,ROCK],[4,SHIP]]){
   const buf=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,buf);gl.bufferData(gl.ARRAY_BUFFER,shape,gl.STATIC_DRAW);
   buffers.set(kind,{buf,count:shape.length/6});
 }
 const draw=scene=>{
   const dpr=Math.min(2,window.devicePixelRatio||1);
   const w=Math.max(1,Math.round(canvas.clientWidth*dpr)),h=Math.max(1,Math.round(canvas.clientHeight*dpr));
   if(canvas.width!==w||canvas.height!==h){canvas.width=w;canvas.height=h;}
   gl.viewport(0,0,w,h);gl.clearColor(.017,.035,.065,1);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);
   if(scene.status!=='ready')return;
   gl.uniform1f(loc.yaw,scene.headingDegrees*Math.PI/180);
   gl.uniform1f(loc.aspect,w/h);gl.uniform1f(loc.focal,1/Math.tan(72*Math.PI/360));
   for(const o of scene.objects){
     const mesh=buffers.get(o.kind);if(!mesh)continue;
     gl.bindBuffer(gl.ARRAY_BUFFER,mesh.buf);
     gl.enableVertexAttribArray(loc.p);gl.vertexAttribPointer(loc.p,3,gl.FLOAT,false,24,0);
     gl.enableVertexAttribArray(loc.n);gl.vertexAttribPointer(loc.n,3,gl.FLOAT,false,24,12);
     gl.uniform3f(loc.offset,o.x,0,o.z);
     gl.uniform3f(loc.scale,o.size,o.size,o.size);
     gl.uniform3fv(loc.color,o.color);gl.drawArrays(gl.TRIANGLES,0,mesh.count);
   }
 };
 return {draw,destroy(){for(const m of buffers.values())gl.deleteBuffer(m.buf);gl.deleteProgram(program);}};
}
function init(){
 const canvas=document.getElementById('fpsSpace'),map=document.getElementById('space');
 const start=document.getElementById('pilotView3D'),end=document.getElementById('pilotView2D');
 const bar=document.getElementById('pilotViewControls'),status=document.getElementById('pilotViewStatus');
 if(!canvas||!map||!start||!end||!bar)return;
 let renderer=null,active=false,frame=null;
 function switchMode(next){
   if(next&&!window.GalaxyColonyMode)return;
   if(next&&!renderer){try{renderer=createRenderer(canvas);}catch(e){status.textContent=e.message;return;}}
   active=next;canvas.hidden=!next;map.hidden=next;
   document.getElementById('pilotCrosshair').hidden=!next;
   start.setAttribute('aria-pressed',String(next));end.setAttribute('aria-pressed',String(!next));
   if(next&&frame)update(frame);
   else status.textContent='2D tactical map · native simulation unchanged';
 }
 function update(state){
   frame=state;
   bar.hidden=!window.GalaxyColonyMode;
   if(!window.GalaxyColonyMode){if(active)switchMode(false);return;}
   if(!active)return;
   const scene=project(state,{activeId:window.GalaxyActivePilotedShipId,
      homeId:window.GalaxyColonyHomeShipId,heading:window.GalaxyFirstPersonVerifiedHeading,
      headingShipId:window.GalaxyShipHeadingShipId});
   renderer.draw(scene);
   status.textContent=scene.status==='ready'?
     `First-person cockpit · system ${scene.system} · ${scene.objects.length} physical contacts (untextured proxies; XY layer)`:
     'First-person camera paused: '+scene.status+' (no inferred ship identity or heading)';
 }
 start.addEventListener('click',()=>switchMode(true));end.addEventListener('click',()=>switchMode(false));
 return {update,mode:()=>active?'first-person-cockpit':'tactical-map'};
}
return {project,createRenderer,init};
});
