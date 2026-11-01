/* ECON-02B2d4c2: same-origin development-host controls. Never place trusted
   owner IDs in the browser; the native host owns player/ship/site identity. */
(()=>{
'use strict';
const ui=id=>document.getElementById(id);
const root=location.pathname.slice(0,location.pathname.lastIndexOf('/')+1);
// Offline file:// use is still fully supported without native controls.
function offline(text){ui('liveStatus').textContent=text;for(const id of ['liveRegister','liveStep','liveCancel','liveRefresh'])ui(id).disabled=true;}
if(location.protocol==='file:'){offline('Offline design mode — no colony connected');return;}
const secret=root.match(/^\/session\/([0-9a-f]{64})\/$/)?.[1];
if(!secret){offline('No trusted host session');return;}
let active=null, connected=false, busy=false;
const setText=(id,text)=>ui(id).textContent=text;
function feedback(text,problem=false){setText('liveNotice',text);ui('liveNotice').style.color=problem?'#ffafa8':'#9bdccf';}
function updatePlan(p){
  active=p||null;
  setText('livePlan',p?`Plan #${p.id} · ${p.completed}/${p.total} steps · ${p.modulesRemaining} modules remaining${p.cancelled?' · cancelled':p.finished?' · finished':p.conflicting?' · conflict':''}`:'No active plan');
  ui('liveStep').disabled=ui('liveCancel').disabled=!p||p.cancelled||p.finished||p.conflicting;
  ui('liveRegister').disabled=!!p&&!p.cancelled&&!p.finished;
}
async function api(endpoint,body){
  const opts=body===undefined?{}:{method:'POST',headers:{'Content-Type':'application/json','X-SG-Session':secret},body:JSON.stringify(body)};
  const response=await fetch(root+'api/'+endpoint,{cache:'no-store',...opts});
  const data=await response.json();if(!response.ok||!data.ok)throw Error(data.error||`HTTP ${response.status}`);
  return data;
}
async function load(){
  const [intro,list]=await Promise.all([api('hello'),api('plans')]);
  connected=true;
  setText('liveStatus',`Connected · Outpost ${intro.site} · Ship ${intro.ship} · ${intro.modulesAvailable} modules available`);
  const plans=list.plans.filter(p=>p.site===intro.site);
  updatePlan(plans.find(p=>!p.cancelled&&!p.finished)||plans.at(-1)||null);
}
async function action(fn){if(busy)return;busy=true;
  for(const b of [ui('liveStep'),ui('liveRegister'),ui('liveCancel'),ui('liveRefresh')])b.disabled=true;
  try{await fn();await load();}catch(e){feedback(e.message,true);}
  finally{busy=false;if(active)updatePlan(active);else ui('liveRegister').disabled=false;ui('liveRefresh').disabled=false;}
}
ui('liveRefresh').onclick=()=>action(async()=>{feedback('World state refreshed.');});
ui('liveRegister').onclick=()=>action(async()=>{
  const editor=window.SingleGalaxyEditorInstance?.();
  if(!editor)throw Error('Editor not ready');
  const audit=editor.audit();if(!audit.valid)throw Error(audit.errors.join('; '));
  if(!window.confirm(`Authorize ${audit.jobs} construction steps requiring ${audit.modules} modules at the connected outpost?\n\nWalls, doors, and floors remain design-only overlays.`))return;
  const result=await api('register',{layout:editor.canonical()});
  updatePlan(result.plan);feedback(`Host registered construction plan #${result.plan.id}. No materials spent yet.`);
});
ui('liveStep').onclick=()=>action(async()=>{
  if(!active)throw Error('No active plan');
  const result=await api('step',{id:active.id});
  updatePlan(result.plan);feedback(result.event.applied?'Construction step committed to SQLite.':'Construction blocked: required materials or other game condition unavailable.',!result.event.applied);
});
ui('liveCancel').onclick=()=>action(async()=>{
  if(!active)throw Error('No active plan');
  if(!window.confirm('Cancel unfinished construction? Already-built equipment remains in the colony and will not be refunded.'))return;
  const result=await api('cancel',{id:active.id});updatePlan(result.plan);feedback('Unfinished construction cancelled durably.');
});
load().catch(e=>{feedback(e.message,true);setText('liveStatus','Native host unavailable');});
})();
