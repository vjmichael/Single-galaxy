/* v64 — same-origin player session and native shipment projection in the colony UI. */
(()=>{'use strict';
const el=id=>document.getElementById(id);
const panel=el('colonySessionPanel');if(!panel)return;
let csrf='',principal=null;
const notice=s=>{el('colonyAuthMessage').textContent=s;};
async function json(url,opts={}){
  const r=await fetch(url,{cache:'no-store',credentials:'same-origin',...opts});
  const data=await r.json();if(!r.ok||data.ok===false)throw Error(data.error||'Request denied');
  return data;
}
function session(s){
  principal=s;csrf=s.csrf;el('colonySignIn').hidden=true;el('colonyIdentity').hidden=false;
  el('colonyPlayerLabel').textContent=`${s.username} · Player ${s.playerId}${s.playerId===7?' · Colony pilot':' · Freight participant'}`;
  notice(s.playerId===7?'Colony controls available.':'Shipment view only. This colony ship belongs to player 7.');
}
function render(rows){
  const target=el('colonyShipmentList');target.replaceChildren();
  if(!rows.length){const p=document.createElement('p');p.className='help';p.textContent='No recorded shipments involving this player.';target.append(p);return;}
  for(const r of rows){
    const card=document.createElement('article');card.className='card pad';
    const title=document.createElement('h2');title.textContent=r.id;
    const status=document.createElement('p');status.textContent=`${r.phase.replaceAll('_',' ')} · ${r.role.replaceAll('_',' ')}`;
    const route=document.createElement('p');route.className='help';route.textContent=`${r.quantityKg} kg ${r.material.replaceAll('_',' ')} · Warehouse ${r.originSite} → Warehouse ${r.destinationSite} · Carrier ship ${r.carrierShip}`;
    const receipt=document.createElement('p');receipt.className='help';receipt.textContent=`Loaded: ${r.loaded?'yes':'no'} · Delivered: ${r.delivered?'yes':'no'} · Settled: ${r.settled?'yes':'no'}`;
    card.append(title,status,route,receipt);target.append(card);
  }
}
async function refresh(){if(!principal)return;
  try{const r=await json('/api/shipments');render(r.shipments);}
  catch(e){notice('Shipment status unavailable: '+e.message);}}
el('colonySignIn').addEventListener('submit',async event=>{
  event.preventDefault();
  try{const s=await json('/api/login',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({username:el('colonyUsername').value,password:el('colonyPassword').value})});
    el('colonyPassword').value='';session(s);await refresh();if(s.playerId===7)window.GalaxyReconnect?.();
  }catch(e){el('colonyPassword').value='';notice('Login rejected: '+e.message);}
});
el('colonySignOut').addEventListener('click',async()=>{
  try{await json('/api/logout',{method:'POST',headers:{'Content-Type':'application/json','X-SG-CSRF':csrf},body:'{}'});}
  catch(e){notice(e.message);return;}
  csrf='';principal=null;el('colonyIdentity').hidden=true;el('colonySignIn').hidden=false;
  el('colonyShipmentList').replaceChildren();notice('Signed out.');
  window.GalaxyDisconnect?.();
});
el('colonyShipmentRefresh').addEventListener('click',refresh);
json('/api/session').then(s=>{panel.hidden=false;session(s);refresh();if(s.playerId===7)window.GalaxyReconnect?.();})
.catch(async()=>{
  try{const r=await fetch('/api/session',{cache:'no-store'});
    if(r.status===404)panel.hidden=true;
    else {panel.hidden=false;window.GalaxyDisconnect?.();} }
  catch{panel.hidden=true;}
});
})();
