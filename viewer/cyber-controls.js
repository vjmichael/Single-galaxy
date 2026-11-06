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
   if(type==='CYBER_SENSORS' && v.length===8){
     const ship=validNumber(v[4]),
       stealth=validNumber(v[5],1),counter=validNumber(v[6],1),
       scanner=validNumber(v[7],1);
     if(!ship)throw Error('Invalid original sensor ship');
     return {tick,type,ship,stealth:!!stealth,counter:!!counter,
       scanner:!!scanner};
   }
   if(type==='CYBER_DEFENSE' && v.length===8){
     const ship=validNumber(v[4]),remaining=validNumber(v[5],8),
       source=validNumber(v[6]),exposed=validNumber(v[7],16);
     if(!ship || (remaining===0 && (source!==0 || exposed!==0)) ||
        (remaining>0 && (!source || !exposed || source===ship)))
       throw Error('Invalid committed defense state');
     return {tick,type,ship,remaining,source,exposed};
   }
   if(type==='CYBER_INCOMING' && v.length===8){
     const ship=validNumber(v[4]),source=validNumber(v[5]),
       id=validNumber(v[6]),result=v[7];
     if(!ship || !source || !id || source===ship ||
        !/^[A-Z_]+$/.test(result))
       throw Error('Invalid committed incoming cyber event');
     return {tick,type,ship,source,id,result};
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
 let status=null,defense=null,sensors=null,modules=new Map(),targets=new Map();
 function reset(){
   status=null;defense=null;sensors=null;modules.clear();targets.clear();
   e('cyberStatus').textContent='Waiting for committed ship electronics.';
   e('cyberDefense').textContent='Waiting for committed defense state.';
   e('cyberAlert').textContent='No incoming cyber attack reported.';
   e('cyberSensorStatus').textContent='Awaiting real fitted sensors and stealth status.';
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
     ['cyberRemoveFirewall',!ready||!status.colony||!firewall],
     ['cyberFitStealth',!ready||!status.colony||!sensors||
       sensors.tick!==status.tick||sensors.stealth],
     ['cyberRemoveStealth',!ready||!status.colony||!sensors||
       sensors.tick!==status.tick||!sensors.stealth],
     ['cyberFitCounterSensor',!ready||!status.colony||!sensors||
       sensors.tick!==status.tick||sensors.counter],
     ['cyberRemoveCounterSensor',!ready||!status.colony||!sensors||
       sensors.tick!==status.tick||!sensors.counter]
   ])e(id).disabled=disabled;
   if(ready){
     e('cyberStatus').textContent='Ship #'+status.ship+
       ' · '+(emitter?'intrusion emitter fitted':'no intrusion emitter')+
       ' · '+(firewall?'firewall fitted':'no firewall')+
       ' · emitter '+(emitterPowered?'ONLINE':'OFFLINE')+
       ' · emission cooldown '+status.cooldown+' tick(s)';
   }
   e('cyberDefense').textContent=defense && status && defense.tick===status.tick && defense.remaining>0?
     'CONFUSION ACTIVE · '+defense.remaining+' tick(s) remaining · source ship #'+
       defense.source+' · '+defense.exposed+' exposed module(s). '+
       'You can isolate a hardpoint below to prevent remote toggling.':
     'No active Confusion on this ship. '+(firewall?
       'Passive firewall fitted.':'No passive firewall fitted.');
   e('cyberSensorStatus').textContent=sensors &&
     sensors.tick===status?.tick?
       (sensors.stealth?'Stealth baffle FITTED':'Stealth baffle not fitted')+
       ' · '+(sensors.counter?'Counter-ECM sensor array FITTED':
         'Standard passive sensors')+
       ' · '+(sensors.scanner?'Survey scanner ONLINE':
         'No powered survey scanner')+
       ' · passive scans do not reveal authenticated identities.':
       'Awaiting this tick\'s native stealth and sensor status.';
   const list=e('cyberModuleList');
   list.replaceChildren();
   for(const slot of [0,1]){
     const m=modules.get(slot);
     if(!m || m.tick!==status?.tick)continue;
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
     ...[...targets.values()].filter(v=>v.tick===status?.tick)
       .sort((a,b)=>a.id-b.id)
       .map(v=>new Option('Ship #'+v.id+' · '+v.distance+' mm',String(v.id))));
   if([...select.options].some(o=>o.value===prev))select.value=prev;
   e('cyberAttack').disabled=!canAttack(status,Number(select.value),emitterPowered) ||
      targets.get(Number(select.value))?.tick!==status?.tick;
 }
 function accept(raw){
   if(typeof raw!=='string'||!raw.includes('|CYBER_'))return false;
   const data=parse(raw);
   if(!data)return false;
   if(data.type==='CYBER_STATUS'){
     if(status?.ship!==data.ship){
       modules.clear();targets.clear();defense=null;sensors=null;
       e('cyberAlert').textContent='No incoming cyber attack reported.';
     }else if(status.tick!==data.tick){
       defense=null;sensors=null; // current committed tick only
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
   }else if(data.type==='CYBER_DEFENSE'){
     if(data.tick===status.tick)defense=data;
   }else if(data.type==='CYBER_SENSORS'){
     if(data.tick===status.tick)sensors=data;
   }else if(data.type==='CYBER_INCOMING'){
     if(data.result==='APPLIED')
       e('cyberAlert').textContent='INCOMING CYBER ATTACK · ship #'+data.source+
         ' disrupted your modules (native command #'+data.id+').';
     else if(data.result==='ATTACK_RESISTED')
       e('cyberAlert').textContent='CYBER ATTACK RESISTED · ship #'+data.source+
         ' did not disrupt your modules (native command #'+data.id+').';
     else
       e('cyberAlert').textContent='Enemy cyber attempt · ship #'+data.source+
         ' · native '+data.result.replaceAll('_',' ').toLowerCase()+
         ' (command #'+data.id+').';
   }
   if(data.type==='CYBER_RESULT')
     e('cyberFeedback').textContent='Native command #'+data.id+
       ' completed with '+data.result.replaceAll('_',' ').toLowerCase()+'.';
   update();
   return true;
 }
 for(const [id,device,install] of [
   ['cyberFitEmitter','emitter',true],['cyberRemoveEmitter','emitter',false],
   ['cyberFitFirewall','firewall',true],['cyberRemoveFirewall','firewall',false],
   ['cyberFitStealth','stealth',true],['cyberRemoveStealth','stealth',false],
   ['cyberFitCounterSensor','counterSensor',true],
   ['cyberRemoveCounterSensor','counterSensor',false]
 ])e(id).addEventListener('click',()=>send('colonyCyberFit',{device,install}));
 e('cyberAttack').addEventListener('click',()=>{
   const target=Number(e('cyberTarget').value);
   const powered=[...modules.values()].some(m=>m.tick===status?.tick &&
     m.hardware===3 && m.effective && !m.isolated);
   if(canAttack(status,target,powered)&&targets.get(target)?.tick===status?.tick)
     void send('colonyCyberAttack',{target});
 });
 e('cyberTarget').addEventListener('change',update);
 reset();
 return {parse,canAttack,accept,reset};
});
