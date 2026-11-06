/* SHIP-STATUS-05: the visible cyber cockpit reads ONLY committed native
 * COLONY telemetry. HTTP requests contain a typed action, never a player ID,
 * command receipt, source ship ID, combat verdict or game tick. */
(function(root,factory){
 'use strict';
 const api=factory();
 if(typeof module!=='undefined'&&module.exports)module.exports=api;
 root.GalaxyCyberControls=api;
})(typeof window!=='undefined'?window:globalThis,function(){
 'use strict';
 const validNumber=(raw,max=Number.MAX_SAFE_INTEGER)=>{
   if(!/^(0|[1-9][0-9]*)$/.test(raw))throw Error('Invalid cyber number');
   const value=Number(raw);
   if(!Number.isSafeInteger(value)||value>max)throw Error('Cyber number overflow');
   return value;
 };
 function parse(raw){
   if(typeof raw!=='string'||raw.length>180 ||
      !/^COLONY\|1\|[0-9]+\|CYBER_[A-Z_]+\|/.test(raw) ||
      !/^[A-Za-z0-9_|-]+$/.test(raw))
      throw Error('Not verified cyber telemetry');
   const v=raw.split('|'),tick=validNumber(v[2]),type=v[3];
   if(type==='CYBER_STATUS' && v.length===10){
     const ship=validNumber(v[4]),emitter=validNumber(v[5],1),
       firewall=validNumber(v[6],1),cooldown=validNumber(v[7],5),
       colony=validNumber(v[8],1);
     // Current host emits nine tokens: COLONY,1,tick,type and five fields.
     throw Error('Cyber status overlong');
   }
   if(type==='CYBER_STATUS' && v.length===9){
     const ship=validNumber(v[4]),emitter=validNumber(v[5],1),
       firewall=validNumber(v[6],1),cooldown=validNumber(v[7],5),
       colony=validNumber(v[8],1);
     if(!ship)throw Error('Missing ship');
     return {tick,type,ship,emitter:!!emitter,firewall:!!firewall,
       cooldown,colony:!!colony};
   }
   if(type==='CYBER_MODULE' && v.length===10){
     const ship=validNumber(v[4]),slot=validNumber(v[5],1),
       hardware=validNumber(v[6],3),requested=validNumber(v[7],1),
       effective=validNumber(v[8],1),isolated=validNumber(v[9],1);
     if(!ship)return null;
     if(!hardware && (effective||isolated))
       throw Error('Invalid empty module state');
     return {tick,type,ship,slot,hardware,requested:!!requested,
       effective:!!effective,isolated:!!isolated};
   }
   if(type==='CYBER_TARGETS_RESET' && v.length===5)
     return {tick,type,ship:validNumber(v[4])};
   if(type==='CYBER_TARGET' && v.length===7){
     const ship=validNumber(v[4]),id=validNumber(v[5]),distance=validNumber(v[6],8000);
     if(!ship||!id||ship===id)throw Error('Invalid physical target');
     return {tick,type,ship,id,distance};
   }
   if(type==='CYBER_RESULT' && v.length===7){
     const id=validNumber(v[4]),action=validNumber(v[5],3);
     const result=v[6];
     if(!id || !/^[A-Z_]+$/.test(result))throw Error('Invalid native outcome');
     return {tick,type,id,action,result};
   }
   return null;
 }
 // Stateless policy helper: never infer a successful native command from a
 // queued HTTP reply or from a button being clicked.
 const canAttack=(state,target,emitterPowered)=>!!state&&state.emitter&&
   emitterPowered===true&&state.cooldown===0&&
   Number.isSafeInteger(target)&&target>0;
 if(typeof document==='undefined')return {parse,canAttack};
 const e=id=>document.getElementById(id);
 let status=null,modules=new Map(),targets=new Map(),lastCycle=-1;
 function reset(){
   status=null;modules.clear();targets.clear();lastCycle=-1;
   e('cyberStatus').textContent='Waiting for committed ship electronics.';
   e('cyberFeedback').textContent='No attack queued.';
   e('cyberModuleList').replaceChildren();
   e('cyberTarget').replaceChildren(new Option('No valid local target',''));
   update();
 }
 function send(action,extra={}){
   e('cyberFeedback').textContent='Native operation requested; awaiting committed outcome.';
   return window.GalaxyColonyCommand?.(action,extra);
 }
 function update(){
   const ready=!!status, emitter=!!status?.emitter,firewall=!!status?.firewall;
   // Only this committed tick's actual hardware result can enable an attack.
   // Fitted, but manually OFF, isolated, or CONFUSED-OFF is not operational.
   const emitterPowered=[...modules.values()].some(m=>
     m.tick===status?.tick && m.hardware===3 && m.effective && !m.isolated);
   for(const [id,disabled] of [
     ['cyberFitEmitter',!ready||!status.colony||emitter],
     ['cyberRemoveEmitter',!ready||!status.colony||!emitter],
     ['cyberFitFirewall',!ready||!status.colony||firewall],
     ['cyberRemoveFirewall',!ready||!status.colony||!firewall]
   ])e(id).disabled=disabled;
   if(ready){
     e('cyberStatus').textContent='Ship #'+status.ship+
       ' · '+(emitter?'intrusion emitter fitted':'no intrusion emitter')+
       ' · '+(firewall?'firewall fitted':'no firewall')+
       ' · emitter '+(emitterPowered?'ONLINE':'OFFLINE')+
       ' · emission cooldown '+status.cooldown+' tick(s)';
   }
   const list=e('cyberModuleList');
   list.replaceChildren();
   for(const slot of [0,1]){
     const m=modules.get(slot);
     if(!m)continue;
     const row=document.createElement('div');row.className='controls';
     row.style.padding='5px 0';row.style.gap='6px';
     const label=document.createElement('span');
     label.textContent='Hardpoint '+(slot+1)+' · '+
       ({0:'Empty',1:'Scanner',2:'Probe launcher',3:'Intrusion emitter'}[m.hardware]||
        'Equipment')+' · '+
       (m.effective?'ON':'OFF')+
       (m.effective!==m.requested && !m.isolated?' · CONFUSED':'')+
       (m.isolated?' · ISOLATED':'');
     row.append(label);
     const toggle=(name,action,enabled)=>{
       const button=document.createElement('button');
       button.type='button';button.textContent=name;
       button.disabled=!m.hardware;
       button.addEventListener('click',()=>send(action,{slot,enabled}));
       row.append(button);
     };
     toggle(m.requested?'Power off':'Power on','colonyCyberPower',!m.requested);
     toggle(m.isolated?'Reconnect':'Isolate','colonyCyberIsolate',!m.isolated);
     list.append(row);
   }
   const select=e('cyberTarget'),prev=select.value;
   select.replaceChildren(new Option('Select observed hostile ship',''),
     ...[...targets.values()].sort((a,b)=>a.id-b.id)
       .map(v=>new Option('Ship #'+v.id+' · '+v.distance+' mm',String(v.id))));
   if([...select.options].some(o=>o.value===prev))select.value=prev;
   e('cyberAttack').disabled=!canAttack(status,Number(select.value),emitterPowered) ||
      !targets.has(Number(select.value));
 }
 function accept(raw){
   if(typeof raw!=='string'||!raw.includes('|CYBER_'))return false;
   const data=parse(raw);
   if(!data)return false;
   if(data.type==='CYBER_STATUS'){
     if(status?.ship!==data.ship){
       modules.clear();targets.clear();
     }
     status=data;
   }else if(!status||data.ship!==status.ship){
     if(data.type!=='CYBER_RESULT')return true;
   }else if(data.type==='CYBER_MODULE'){
     modules.set(data.slot,data);
   }else if(data.type==='CYBER_TARGETS_RESET'){
     targets.clear();
   }else if(data.type==='CYBER_TARGET'){
     targets.set(data.id,data);
   }
   if(data.type==='CYBER_RESULT')
     e('cyberFeedback').textContent='Native command #'+data.id+
       ' completed with '+data.result.replaceAll('_',' ').toLowerCase()+'.';
   update();
   return true;
 }
 for(const [id,device,install] of [
   ['cyberFitEmitter','emitter',true],['cyberRemoveEmitter','emitter',false],
   ['cyberFitFirewall','firewall',true],['cyberRemoveFirewall','firewall',false]
 ])e(id).addEventListener('click',()=>send('colonyCyberFit',{device,install}));
 e('cyberAttack').addEventListener('click',()=>{
   const target=Number(e('cyberTarget').value);
   const powered=[...modules.values()].some(m=>m.tick===status?.tick &&
     m.hardware===3 && m.effective && !m.isolated);
   if(canAttack(status,target,powered)&&targets.has(target))
     void send('colonyCyberAttack',{target});
 });
 e('cyberTarget').addEventListener('change',update);
 reset();
 return {parse,canAttack,accept,reset};
});
