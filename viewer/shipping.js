// Read-only player-facing shipment-status interface. Deliberately never
// accepts a player ID from the URL, DOM or request body.
(()=>{'use strict';
const el=x=>document.getElementById(x);let csrf='',canWrite=false;
function notice(t){el('notice').textContent=t;}
async function request(url,options={}){const r=await fetch(url,{credentials:'same-origin',cache:'no-store',...options});
 const data=await r.json();if(!r.ok||!data.ok)throw Error(data.error||'Host error');return data;}
function showSession(s){csrf=s.csrf;canWrite=!!s.writes;el('signin').hidden=true;el('dashboard').hidden=false;el('who').textContent=`${s.username} · Player ${s.playerId}`;}
const phases={awaiting_approval:'Awaiting approvals',approved_awaiting_dispatch:'Ready for dispatch',dispatched_awaiting_loading:'Dispatched · awaiting loading',in_transit:'In transit',return_requested:'Return requested',redirect_requested:'Alternate route requested',revoked:'Revoked',returned_awaiting_settlement:'Returned · awaiting settlement',delivered_awaiting_settlement:'Delivered · awaiting settlement',redirected_awaiting_settlement:'Redirected delivery · awaiting settlement',returned_settled:'Returned and settled',delivered_settled:'Delivered and settled',redirected_settled:'Redirected and settled'};
function render(items){const list=el('list');list.replaceChildren();if(!items.length){const p=document.createElement('p');p.textContent='No shipments involving this player.';list.append(p);return;}
 for(const item of items){const article=document.createElement('article');article.className='shipment';
  const title=document.createElement('h2');title.textContent=item.id;
  const status=document.createElement('p');status.className='state';status.textContent=phases[item.phase]||item.phase;
  const info=document.createElement('p');info.textContent=`${item.quantityKg} kg ${item.material.replaceAll('_',' ')} · ${item.role.replaceAll('_',' ')} · Carrier ship ${item.carrierShip}`;
  const route=document.createElement('small');route.textContent=`Origin warehouse ${item.originSite} → destination warehouse ${item.destinationSite}${item.redirected?' (redirected)':''}`;
  article.append(title,status,info,route);
  if(canWrite){
   const actions=[];
   if(item.phase==='awaiting_approval'&&!item.myApprovalRecorded&&['source','carrier','receiver'].includes(item.role))actions.push(['Approve my role','approve',{role:{source:1,carrier:2,receiver:3}[item.role]}]);
   if(item.phase==='approved_awaiting_dispatch'&&item.role==='carrier')actions.push(['Dispatch','dispatch',{dispatchId:item.id+'-dispatch'}]);
   if(item.phase==='dispatched_awaiting_loading'&&item.role==='carrier')actions.push(['Load cargo','load',{}]);
   if(item.phase==='delivered_awaiting_settlement'&&!item.mySettlementRecorded&&['carrier','receiver'].includes(item.role))actions.push(['Acknowledge delivery','ack',{role:item.role==='carrier'?2:3}]);
   if(item.phase==='redirected_awaiting_settlement'&&!item.mySettlementRecorded&&['carrier','alternate_receiver'].includes(item.role))actions.push(['Acknowledge redirected delivery','ack_redirect',{role:item.role==='carrier'?2:4}]);
   if(item.phase==='returned_awaiting_settlement'&&!item.mySettlementRecorded&&['carrier','source'].includes(item.role))actions.push(['Acknowledge return','ack',{role:item.role==='carrier'?2:1}]);
   for(const [label,method,fields] of actions){const b=document.createElement('button');b.textContent=label;b.type='button';
    b.addEventListener('click',async()=>{b.disabled=true;try{await request('/api/shipment/'+method,{method:'POST',headers:{'Content-Type':'application/json','X-SG-CSRF':csrf},body:JSON.stringify({id:item.id,...fields})});await refresh();}catch(e){notice(e.message);}finally{b.disabled=false;}});
    article.append(b);}
  }
  list.append(article);
 }}
async function refresh(){try{const d=await request('/api/shipments');render(d.shipments);notice(`Updated ${d.shipments.length} shipment(s).`);}catch(e){notice(e.message);}}
el('loginForm').addEventListener('submit',async e=>{e.preventDefault();try{
 const username=el('username').value,password=el('password').value;
 const session=await request('/api/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({username,password})});
 el('password').value='';showSession(session);await refresh();}catch(err){el('password').value='';notice(err.message);}});
el('refresh').addEventListener('click',refresh);
el('logout').addEventListener('click',async()=>{try{await request('/api/logout',{method:'POST',headers:{'Content-Type':'application/json','X-SG-CSRF':csrf},body:'{}'});}catch(e){notice(e.message);}
 csrf='';el('dashboard').hidden=true;el('signin').hidden=false;el('list').replaceChildren();});
request('/api/session').then(s=>{showSession(s);return refresh();}).catch(()=>{el('dashboard').hidden=true;el('signin').hidden=false;});
})();
