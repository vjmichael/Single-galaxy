/* SHIP-STATUS-07D: owner-scoped native security case + voluntary hail UI.
 * No client-created principal, eyewitness, evidence, verdict or arrest.
 * Use actual committed native COLONY security receipts; POST 202 is pending.
 */
(function(root,factory){
  const api=factory();
  if(typeof module!=='undefined'&&module.exports)module.exports=api;
  if(root)root.GalaxySecurityDesk=api;
})(typeof window!=='undefined'?window:null,function(){
 'use strict';
 const whole=(v,max=Number.MAX_SAFE_INTEGER)=>{
   if(!/^(0|[1-9][0-9]*)$/.test(v) || v.length>19)
     throw Error('Invalid native security number');
   const n=Number(v);
   if(!Number.isSafeInteger(n)||n<0||n>max)
     throw Error('Out-of-range security number');
   return n;
 };
 const caseKey=v=>{
   if(!/^[1-9][0-9]{0,18}$/.test(v) ||
      BigInt(v)>=(1n<<62n))
     throw Error('Invalid original case identifier');
   return v; // Never round a 62-bit native SQLite identifier through Number.
 };
 const decodeHex=raw=>{
   if(raw.length>512||raw.length%2!==0||!/^[0-9a-f]*$/.test(raw))
     throw Error('Invalid original sensor description');
   let result='';
   for(let i=0;i<raw.length;i+=2){
     const n=parseInt(raw.slice(i,i+2),16);
     if(n<32||n>126||n===60||n===62||n===38)
       throw Error('Invalid sensor description character');
     result+=String.fromCharCode(n);
   }
   return result;
 };
 function parse(raw){
   if(typeof raw!=='string' ||
      !/^COLONY\|1\|[0-9]+\|SECURITY_[A-Z_]+(?:\||$)/.test(raw) ||
      raw.length>1300)return null;
   const v=raw.split('|');
   const tick=whole(v[2],1000000000000000),type=v[3];
   if((type==='SECURITY_CASES_RESET' ||
       type==='SECURITY_CONTACTS_RESET' ||
       type==='SECURITY_NOTICES_RESET' ||
       type==='SECURITY_DESTINATIONS_RESET' ||
       type==='SECURITY_REFRESH_DONE') && v.length===4)
     return {tick,type};
   if(type==='SECURITY_STATUS' && v.length===9){
     const home=whole(v[4]),piloted=whole(v[5]),
       claimed=whole(v[6],1),count=whole(v[7],10000),
       tier=whole(v[8],3);
     if(!home||!piloted)throw Error('Missing real security colony');
     return {tick,type,home,piloted,claimed:!!claimed,count,tier};
   }
   if(type==='SECURITY_CASE' && v.length===11){
     const id=caseKey(v[4]),originalTick=whole(v[5]),
       applied=whole(v[6],1),detail=whole(v[7],3),
       description=decodeHex(v[8]),controller=whole(v[9]),
       hail=whole(v[10]);
     if(!detail || !description || (controller===0)!==(hail===0))
       throw Error('Invalid native security case attribution');
     return {tick,type,id,originalTick,applied:!!applied,detail,
       description,controller,hail};
   }
   if(type==='SECURITY_CONTACT' && v.length===7){
     const ship=whole(v[4]),detail=whole(v[5],3),
       description=decodeHex(v[6]);
     if(!ship||!detail||!description)
       throw Error('Invalid native physical contact');
     return {tick,type,ship,detail,description};
   }
   if(type==='SECURITY_NOTICE' && v.length===7){
     const colony=whole(v[4]),ship=whole(v[5]),
       event=whole(v[6]);
     if(!colony||!ship||!event)throw Error('Invalid real notice');
     return {tick,type,colony,ship,event};
   }
   if(type==='SECURITY_DESTINATION' && v.length===5){
     const colony=whole(v[4]);
     if(!colony)throw Error('Invalid real jurisdiction');
     return {tick,type,colony};
   }
   if(type==='SECURITY_RESULT' && v.length===8){
     const operation=v[4],result=v[5],receipt=whole(v[6]),
       linked=whole(v[7],1);
     if(!/^SECURITY_(CLAIM|WARN|HAIL|REPLY|PATROL)$/.test(operation) ||
        !['ACCEPTED','DENIED'].includes(result) ||
        (result==='ACCEPTED')!==(receipt>0) ||
        (linked && result!=='ACCEPTED'))
       throw Error('Forged security result');
     return {tick,type,operation,result,receipt,linked:!!linked};
   }
   if(type==='SECURITY_CASE_FILED' && v.length===6){
     return {tick,type,owner:whole(v[4]),caseId:caseKey(v[5])};
   }
   if(type==='SECURITY_ERROR' && v.length===5){
     if(!['CASE_INDEX_UNAVAILABLE','CASE_DESK_UNAVAILABLE'].includes(v[4]))
       throw Error('Invalid security failure');
     return {tick,type,reason:v[4]};
   }
   if(type.startsWith('SECURITY_'))
     throw Error('Malformed known security telemetry');
   return null;
 }
 if(typeof document==='undefined')return {parse,decodeHex,caseKey};
 const el=id=>document.getElementById(id);
 let status=null,cases=new Map(),contacts=new Map(),
     notices=new Map(),destinations=new Map();
 function selected(id){return el(id).value;}
 function setOptions(id,placeholder,items){
   const node=el(id),previous=node.value;
   node.replaceChildren(new Option(placeholder,''),
     ...items.map(([label,value])=>new Option(label,String(value))));
   if([...node.options].some(o=>o.value===previous))node.value=previous;
 }
 function update(){
   const active=!!status;
   const tiers=['Unavailable','Record contacts','Watch and hail',
                'Eligible for nonviolent patrol'];
   el('securityDeskStatus').textContent=active?
     'Home colony #'+status.home+' · piloted ship #'+status.piloted+
     ' · '+(status.claimed?'territory claimed':'no territorial claim')+
     ' · '+status.count+' native cyber case(s) · '+tiers[status.tier]:
     'Awaiting authenticated native colony security.';
   el('securityClaim').disabled=!active||status.claimed;
   setOptions('securityContact','No observed contact',
     [...contacts.values()].map(c=>[
       'Observed ship track #'+c.ship+' · '+c.description.replaceAll('|',' · '),
       c.ship]));
   setOptions('securityCaseSelect','No recorded case',
     [...cases.values()].map(c=>[
       'Case #'+c.id+' · '+(c.applied?'modules disrupted':'attempt resisted'),
       c.id]));
   setOptions('securityDestination','No witnessed foreign claim',
     [...destinations.values()].map(c=>['Colony #'+c.colony,c.colony]));
   setOptions('securityNotice','No pending notice',
     [...notices.values()].map(n=>[
       'Colony #'+n.colony+' → your ship #'+n.ship+
       ' · notice #'+n.event,n.colony+':'+n.event]));
   const track=Number(selected('securityContact'));
   const knownCase=cases.get(selected('securityCaseSelect'));
   el('securityWarn').disabled=!active||!status.claimed||
     !contacts.has(track);
   el('securityPatrol').disabled=!active||!knownCase||
     !contacts.has(track)||status.tier!==3;
   el('securityHail').disabled=!active||
     !destinations.has(Number(selected('securityDestination')));
   const notice=notices.get(selected('securityNotice'));
   const replyEnabled=!!(active&&notice&&notice.ship===status.piloted);
   el('securityAcknowledge').disabled=!replyEnabled;
   el('securityRefuse').disabled=!replyEnabled;
   const list=el('securityCaseList');
   list.replaceChildren();
   if(!cases.size){
     const p=document.createElement('div');
     p.textContent='No witnessed cyber incidents in this colony journal.';
     list.append(p);
   }
   for(const c of cases.values()){
     const p=document.createElement('div');
     p.textContent='Case #'+c.id+' · tick '+c.originalTick+
       ' · '+(c.applied?'Confusion applied':'attack resisted')+
       ' · observed: '+c.description.replaceAll('|',' · ')+
       (c.controller?
         ' · LATER authenticated controller #'+c.controller+
         ' (hail #'+c.hail+') — NOT proof of the earlier pilot':
         ' · culprit unidentified');
     list.append(p);
   }
 }
 function reset(){
   status=null;cases=new Map();contacts=new Map();
   notices=new Map();destinations=new Map();
   el('securityDeskFeedback').textContent='No security action submitted.';
   update();
 }
 function send(action,extra={}){
   el('securityDeskFeedback').textContent=
     'Request submitted to authenticated native host; awaiting committed receipt.';
   return window.GalaxyColonyCommand?.(action,extra);
 }
 function accept(raw){
   if(typeof raw!=='string'||!raw.includes('|SECURITY_'))return false;
   const data=parse(raw);
   if(!data)return false;
   switch(data.type){
     case 'SECURITY_STATUS':status=data;break;
     case 'SECURITY_CASES_RESET':cases.clear();break;
     case 'SECURITY_CONTACTS_RESET':contacts.clear();break;
     case 'SECURITY_NOTICES_RESET':notices.clear();break;
     case 'SECURITY_DESTINATIONS_RESET':destinations.clear();break;
     case 'SECURITY_CASE':cases.set(data.id,data);break;
     case 'SECURITY_CONTACT':contacts.set(data.ship,data);break;
     case 'SECURITY_NOTICE':notices.set(data.colony+':'+data.event,data);break;
     case 'SECURITY_DESTINATION':destinations.set(data.colony,data);break;
     case 'SECURITY_CASE_FILED':
       el('securityDeskFeedback').textContent='Native cyber incident #'+
         data.caseId+' recorded. Review the refreshed evidence.';
       break;
     case 'SECURITY_RESULT':
       el('securityDeskFeedback').textContent=
         data.operation.replaceAll('_',' ')+' · native '+data.result+
         (data.receipt?' · receipt #'+data.receipt:'')+
         (data.linked?' · earlier physical hull cases linked to this later authenticated controller':'');
       break;
     case 'SECURITY_ERROR':
       el('securityDeskFeedback').textContent=
         'Security records unavailable: '+data.reason.replaceAll('_',' ');
       break;
   }
   update();
   return true;
 }
 for(const id of ['securityContact','securityCaseSelect',
                     'securityDestination','securityNotice'])
   el(id).addEventListener('change',update);
 el('securityRefresh').addEventListener('click',
   ()=>send('colonySecurityRefresh'));
 el('securityClaim').addEventListener('click',()=>{
   const radius=Number(el('securityClaimRadius').value);
   if(Number.isSafeInteger(radius)&&radius>=1&&radius<=5000)
     void send('colonySecurityClaim',{radius});
 });
 el('securityWarn').addEventListener('click',()=>{
   const ship=Number(selected('securityContact'));
   if(status?.claimed&&contacts.has(ship))
     void send('colonySecurityWarn',{ship});
 });
 el('securityHail').addEventListener('click',()=>{
   const colony=Number(selected('securityDestination'));
   if(destinations.has(colony))
     void send('colonySecurityHail',{colony});
 });
 for(const [id,choice] of [
   ['securityAcknowledge','acknowledge'],['securityRefuse','refuse']
 ]){
   el(id).addEventListener('click',()=>{
     const n=notices.get(selected('securityNotice'));
     if(n&&n.ship===status?.piloted)
       void send('colonySecurityReply',{
         colony:n.colony,notice:n.event,choice});
   });
 }
 el('securityPatrol').addEventListener('click',()=>{
   const record=cases.get(selected('securityCaseSelect')),
     ship=Number(selected('securityContact'));
   if(record&&contacts.has(ship)&&status?.tier===3)
     void send('colonySecurityPatrol',{case:record.id,ship});
 });
 reset();
 return {parse,accept,reset,decodeHex,caseKey};
});
