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
 const nativeId=v=>{
   if(!/^(0|[1-9][0-9]{0,18})$/.test(v) ||
      BigInt(v)>=(1n<<63n))
     throw Error('Invalid native SQLite event identifier');
   return v;
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
       type==='SECURITY_CALL_INBOX_RESET' ||
       type==='SECURITY_CALL_COMPLAINTS_RESET' ||
       type==='SECURITY_NOTIFICATIONS_RESET' ||
       type==='SECURITY_AGENCY_REFERRALS_RESET' ||
       type==='SECURITY_AGENCY_INVESTIGATIONS_RESET' ||
       type==='SECURITY_REFRESH_DONE') && v.length===4)
     return {tick,type};
   if(type==='SECURITY_NOTIFICATION' && v.length===10){
     const sequence=nativeId(v[4]),kind=whole(v[5],15),
       event=nativeId(v[6]),ship=whole(v[7]),
       peerOwner=whole(v[8]),related=nativeId(v[9]);
     if(sequence==='0'||event==='0'||!ship||!kind||
        ([1,5,7,8,9,10,11,12,14].includes(kind)&&peerOwner!==0)||
        ([2,3,6,15].includes(kind)&&peerOwner===0))
       throw Error('Invalid original owner notification');
     return {tick,type,sequence,kind,event,ship,peerOwner,related};
   }
   if(type==='SECURITY_AGENCY_STATUS' && v.length===7){
     const leader=whole(v[4]),officer=whole(v[5]),
       active=whole(v[6],1);
     if((!!leader)!==(!!officer) || (!!leader)!==!!active ||
         (active&&leader===officer))
       throw Error('Invalid original government agency appointment');
     return {tick,type,leader,officer,active:!!active};
   }
   if(type==='SECURITY_AGENCY_INVESTIGATION' && v.length===10){
     const order=nativeId(v[4]),referral=nativeId(v[5]),
       target=whole(v[6]),officer=whole(v[7]),
       admitted=whole(v[8],1),phase=whole(v[9],3);
     if(order==='0'||referral==='0'||!target||!officer||
        (!admitted&&phase!==3))
       throw Error('Invalid native investigation/flight read model');
     return {tick,type,order,referral,target,officer,
       admitted:!!admitted,phase};
   }
   if(type==='SECURITY_AGENCY_REFERRAL' && v.length===10){
     const id=nativeId(v[4]),complaint=nativeId(v[5]),
       caller=whole(v[6]),points=nativeId(v[7]),
       officer=whole(v[8]),reviewer=whole(v[9]);
     if(id==='0'||complaint==='0'||!caller||
        BigInt(points)<3n||!officer||!reviewer||officer===reviewer)
       throw Error('Invalid independent agency referral');
     return {tick,type,id,complaint,caller,points,officer,reviewer};
   }
   if(type==='SECURITY_CALL_INBOX' && v.length===7){
     const request=nativeId(v[4]),ship=whole(v[5]),
       caller=whole(v[6]);
     if(request==='0'||!ship||!caller)
       throw Error('Invalid authenticated inbound ship hail');
     return {tick,type,request,ship,caller};
   }
   if(type==='SECURITY_CALL_PENALTY' && v.length===7){
     const points=nativeId(v[4]),until=whole(v[5],1000000000000000),
       review=whole(v[6],1);
     if((points==='0'&&until!==0)||(review&&BigInt(points)<3n))
       throw Error('Invalid native communications penalty');
     return {tick,type,points,until,review:!!review};
   }
   if(type==='SECURITY_CALL_SANCTION' && v.length===8){
     const ship=whole(v[4]),points=nativeId(v[5]),
       until=whole(v[6],1000000000000000),review=whole(v[7],1);
     if(!ship||(points!=='0' && !until)||
        (review&&BigInt(points)<3n))
       throw Error('Invalid ship hail sanction');
     return {tick,type,ship,points,until,review:!!review};
   }
   if(type==='SECURITY_CALL_REVIEW' && v.length===7){
     const id=nativeId(v[4]),status=whole(v[5],4),
       reviewer=whole(v[6]);
     if(id==='0'||(status===4 && !reviewer))
       throw Error('Invalid native preliminary law review');
     return {tick,type,id,status,reviewer};
   }
   if(type==='SECURITY_CALL_COMPLAINT' && v.length===7){
     const id=nativeId(v[4]),caller=whole(v[5]),
       points=nativeId(v[6]);
     if(id==='0'||!caller||BigInt(points)<3n)
       throw Error('Invalid verified nuisance complaint');
     return {tick,type,id,caller,points};
   }
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
       hail=nativeId(v[10]);
     if(!detail || !description || (controller===0)!==(hail==='0'))
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
       event=nativeId(v[6]);
     if(!colony||!ship||event==='0')throw Error('Invalid real notice');
     return {tick,type,colony,ship,event};
   }
   if(type==='SECURITY_DESTINATION' && v.length===5){
     const colony=whole(v[4]);
     if(!colony)throw Error('Invalid real jurisdiction');
     return {tick,type,colony};
   }
   if(type==='SECURITY_RESULT' && v.length===8){
     const operation=v[4],result=v[5],receipt=nativeId(v[6]),
       linked=whole(v[7],1);
     if(!/^SECURITY_(CLAIM|WARN|HAIL|REPLY|PATROL|CALL|CALL_REPLY|NOTIFICATION_ACK|AGENCY_ESTABLISH|AGENCY_REFER|AGENCY_INVESTIGATE)$/.test(operation) ||
        !['ACCEPTED','DENIED'].includes(result) ||
        (result==='ACCEPTED')!==(receipt!=='0') ||
        (linked && result!=='ACCEPTED'))
       throw Error('Forged security result');
     return {tick,type,operation,result,receipt,linked:!!linked};
   }
   if(type==='SECURITY_CASE_FILED' && v.length===6){
     return {tick,type,owner:whole(v[4]),caseId:caseKey(v[5])};
   }
   if(type==='SECURITY_ERROR' && v.length===5){
     if(!['CASE_INDEX_UNAVAILABLE','CASE_DESK_UNAVAILABLE',
          'COMMS_UNAVAILABLE','NO_OWNED_COLONY',
           'OUTBOX_UNAVAILABLE','AGENCY_UNAVAILABLE'].includes(v[4]))
       throw Error('Invalid security failure');
     return {tick,type,reason:v[4]};
   }
   if(type.startsWith('SECURITY_'))
     throw Error('Malformed known security telemetry');
   return null;
 }
 if(typeof document==='undefined')return {parse,decodeHex,caseKey,nativeId};
 const el=id=>document.getElementById(id);
 let status=null,cases=new Map(),contacts=new Map(),
     notices=new Map(),destinations=new Map(),inbox=new Map(),
     complaints=new Map(),reviews=new Map(),commsPenalty=null,
     notifications=new Map(),pendingAcknowledgments=new Set(),
     agency=null,agencyReferrals=new Map(),agencyInvestigations=new Map();
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
   setOptions('securityCallInbox','No incoming direct hail',
     [...inbox.values()].map(n=>[
       'Authenticated caller #'+n.caller+' in ship #'+n.ship+
         ' · request #'+n.request,n.request]));
   setOptions('securityAgencyComplaint','No verified complaint',
     [...complaints.values()].map(c=>[
       'Complaint #'+c.id+' · '+c.points+' nuisance points',c.id]));
   const agencyReady=active && !!agency?.active;
   el('securityAgencyStatus').textContent=agencyReady?
     'Appointed law agency · leader NPC #'+agency.leader+
     ' · officer NPC #'+agency.officer+
     ' · independent referral only; no warrants or arrests':
     'No currently staffed, authorized colony law agency.';
   el('securityAgencyEstablish').disabled=!active||!status.claimed||
     agencyReady;
   const caseChoice=selected('securityAgencyComplaint');
   el('securityAgencyRefer').disabled=!agencyReady||
     !complaints.has(caseChoice) ||
     [...agencyReferrals.values()].some(r=>r.complaint===caseChoice);
   const agencyNode=el('securityAgencyReferrals');
   agencyNode.replaceChildren();
   if(!agencyReferrals.size){
     const item=document.createElement('div');
     item.textContent='No verified complaints referred to a law agency.';
     agencyNode.append(item);
   }
   for(const c of agencyReferrals.values()){
     const item=document.createElement('div');
     item.textContent='Referral #'+c.id+' · complaint #'+
       c.complaint+' · authenticated caller #'+c.caller+' · '+c.points+
       ' points · agency officer NPC #'+c.officer+
       ' · independent reviewer NPC #'+c.reviewer+
       ' · evidence admitted, NOT a criminal conviction';
     agencyNode.append(item);
   }
   setOptions('securityAgencyInvestigationReferral','No admitted referral',
     [...agencyReferrals.values()].map(r=>[
       'Referral #'+r.id+' · caller account #'+r.caller,r.id]));
   const investigateRef=selected('securityAgencyInvestigationReferral');
   const targetTrack=Number(selected('securityContact'));
   el('securityAgencyInvestigate').disabled=!agencyReady||
     !agencyReferrals.has(investigateRef)||!contacts.has(targetTrack)||
     [...agencyInvestigations.values()].some(i=>
       i.referral===investigateRef && i.admitted);
   const investigationsNode=el('securityAgencyInvestigations');
   investigationsNode.replaceChildren();
   if(!agencyInvestigations.size){
     const item=document.createElement('div');
     item.textContent='No independently authorized native investigation work.';
     investigationsNode.append(item);
   }
   const stages=['planned','physically navigating','physically arrived',
     'native order cancelled'];
   for(const i of agencyInvestigations.values()){
     const item=document.createElement('div');
     item.textContent='Investigation intent #'+i.order+
       ' · verified referral #'+i.referral+
       ' · observed hull #'+i.target+
       ' · named NPC pilot #'+i.officer+
       (i.admitted?' · original NPC flight '+stages[i.phase]:
         ' · durable intent only, native flight NOT admitted; retry needs fresh contact')+
       ' · nonviolent approach only; no search, custody or force';
     investigationsNode.append(item);
   }
   const noticesNode=el('securityNotificationList');
   noticesNode.replaceChildren();
   const names=['','Outgoing hail','Incoming authenticated hail',
     'Your reply','Reply to your hail','Outbound nuisance strike',
     'Incoming verified nuisance strike','Agency evidence referral',
     'Territorial claim/invitation decision',
     'Observed territorial warning','Territorial notice response',
     'Witnessed territorial trespass','Territorial expulsion record',
     'Voluntary authenticated territorial hail',
     'Witnessed anonymous cyber incident (culprit not proven)',
     'Later authenticated hull controller (not proof of earlier attack)'];
   if(!notifications.size){
     const item=document.createElement('div');
     item.textContent='No unacknowledged durable security notifications.';
     noticesNode.append(item);
   }
   for(const n of notifications.values()){
     const item=document.createElement('div');
     item.textContent=names[n.kind]+' · original event #'+n.event+
       ' · ship #'+n.ship+
       (n.peerOwner?' · authenticated counterparty #'+n.peerOwner:'')+
       (n.related!=='0'?' · related original receipt #'+n.related:'')+
       ' · durable delivery #'+n.sequence;
     noticesNode.append(item);
   }
   const track=Number(selected('securityContact'));
   const knownCase=cases.get(selected('securityCaseSelect'));
   el('securityWarn').disabled=!active||!status.claimed||
     !contacts.has(track);
   const muted=!!(commsPenalty&&commsPenalty.until>status?.tick);
   el('securityCallShip').disabled=!active||!contacts.has(track)||muted;
   const pending=inbox.get(selected('securityCallInbox'));
   el('securityIdentifyCaller').disabled=!active||!pending;
   el('securityDeclineCaller').disabled=!active||!pending;
   el('securityCommsPenalty').textContent=commsPenalty&&
      commsPenalty.points!=='0'?
      ('Communications nuisance score '+commsPenalty.points+
       ' · outbound hail mute until game tick '+commsPenalty.until+
       (commsPenalty.review?
        ' · formal security/law-enforcement review eligible (no automatic arrest)':
        ' · repeated unsolicited hails are penalized')):
      'No communications abuse penalty.';
   const complaintsNode=el('securityCommsComplaints');
   complaintsNode.replaceChildren();
   if(!complaints.size){
     const item=document.createElement('div');
     item.textContent='No communications abuse complaints.';
     complaintsNode.append(item);
   }
   for(const c of complaints.values()){
     const item=document.createElement('div');
     const checked=reviews.get(c.id);
     const alreadyReferred=[...agencyReferrals.values()]
       .some(r=>r.complaint===c.id);
     const reviewText=alreadyReferred?
       ' · formally referred to appointed law agency for independent intake; no verdict':
       checked?.status===4?
       ' · named duty reviewer confirmed; awaiting actual law agency':
       checked?.status===3?' · no on-duty reviewer':
       checked?.status===2?' · no territorial jurisdiction claim':
       ' · independent legal review still required';
     item.textContent='Complaint #'+c.id+' · authenticated caller #'+
       c.caller+' · nuisance points '+c.points+
       reviewText+' · not a conviction; NO automatic arrest or armed response';
     complaintsNode.append(item);
   }
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
   notices=new Map();destinations=new Map();inbox=new Map();
   complaints=new Map();reviews=new Map();commsPenalty=null;
   notifications=new Map();pendingAcknowledgments=new Set();
   agency=null;agencyReferrals=new Map();agencyInvestigations=new Map();
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
     case 'SECURITY_CALL_INBOX_RESET':inbox.clear();break;
     case 'SECURITY_CALL_COMPLAINTS_RESET':
       complaints.clear();reviews.clear();break;
     case 'SECURITY_NOTIFICATIONS_RESET':notifications.clear();break;
     case 'SECURITY_AGENCY_REFERRALS_RESET':agencyReferrals.clear();break;
     case 'SECURITY_AGENCY_INVESTIGATIONS_RESET':
       agencyInvestigations.clear();break;
     case 'SECURITY_AGENCY_STATUS':agency=data;break;
     case 'SECURITY_AGENCY_REFERRAL':agencyReferrals.set(data.id,data);break;
     case 'SECURITY_AGENCY_INVESTIGATION':
       agencyInvestigations.set(data.order,data);break;
     case 'SECURITY_NOTIFICATION':notifications.set(data.sequence,data);break;
     case 'SECURITY_CALL_INBOX':inbox.set(data.request,data);break;
     case 'SECURITY_CALL_COMPLAINT':complaints.set(data.id,data);break;
     case 'SECURITY_CALL_REVIEW':reviews.set(data.id,data);break;
     case 'SECURITY_CALL_PENALTY':commsPenalty=data;break;
     case 'SECURITY_CALL_SANCTION':
       commsPenalty={tick:data.tick,points:data.points,
         until:data.until,review:data.review};
       el('securityDeskFeedback').textContent=
         (data.points!=='0'?'Repeated post-identification hail was BLOCKED; '+
           'communications nuisance score '+data.points:
           'Repeated hail suppressed; no second call delivered')+
         ' · outbound mute until tick '+data.until+
         (data.review?' · security/law-enforcement review eligible':'');
       break;
     case 'SECURITY_CASE':cases.set(data.id,data);break;
     case 'SECURITY_CONTACT':contacts.set(data.ship,data);break;
     case 'SECURITY_NOTICE':notices.set(data.colony+':'+data.event,data);break;
     case 'SECURITY_DESTINATION':destinations.set(data.colony,data);break;
     case 'SECURITY_CASE_FILED':
       el('securityDeskFeedback').textContent='Native cyber incident #'+
         data.caseId+' recorded. Review the refreshed evidence.';
       break;
     case 'SECURITY_RESULT':
       if(data.operation==='SECURITY_NOTIFICATION_ACK')break;
       // The generic denied receipt must not erase the native nuisance
       // penalty that was just reported for a repeated identified hail.
       if(!(data.operation==='SECURITY_CALL' &&
            data.result==='DENIED' && commsPenalty?.points>0))
         el('securityDeskFeedback').textContent=
           data.operation.replaceAll('_',' ')+' · native '+data.result+
           (data.receipt!=='0'?' · receipt #'+data.receipt:'')+
           (data.linked?' · earlier physical hull cases linked to this later authenticated controller':'');
       break;
     case 'SECURITY_ERROR':
       el('securityDeskFeedback').textContent=
         'Security records unavailable: '+data.reason.replaceAll('_',' ');
       break;
   }
   update();
   if(data.type==='SECURITY_NOTIFICATION' &&
      !pendingAcknowledgments.has(data.sequence)){
     // A parsed, rendered owner notice is eligible for native ACK.
     // 202 is queued, never claimed as a committed acknowledgment.
     pendingAcknowledgments.add(data.sequence);
     void Promise.resolve(window.GalaxyColonyCommand?.(
       'colonySecurityNotifyAck',{sequence:data.sequence})).catch(()=>{});
   }
   return true;
 }
 for(const id of ['securityContact','securityCaseSelect',
                     'securityDestination','securityNotice',
                     'securityCallInbox','securityAgencyComplaint',
                     'securityAgencyInvestigationReferral'])
   el(id).addEventListener('change',update);
 el('securityRefresh').addEventListener('click',
   ()=>send('colonySecurityRefresh'));
 el('securityAgencyEstablish').addEventListener('click',()=>{
   if(status?.claimed && !agency?.active)
     void send('colonySecurityAgencyEstablish');
 });
 el('securityAgencyInvestigate').addEventListener('click',()=>{
   const referral=selected('securityAgencyInvestigationReferral');
   const ship=Number(selected('securityContact'));
   if(agency?.active && agencyReferrals.has(referral)&&contacts.has(ship))
     void send('colonySecurityAgencyInvestigate',{referral,ship});
 });
 el('securityAgencyRefer').addEventListener('click',()=>{
   const complaint=selected('securityAgencyComplaint');
   if(agency?.active && complaints.has(complaint))
     void send('colonySecurityAgencyRefer',{complaint});
 });
 el('securityClaim').addEventListener('click',()=>{
   const radius=Number(el('securityClaimRadius').value);
   if(Number.isSafeInteger(radius)&&radius>=1&&radius<=5000)
     void send('colonySecurityClaim',{radius});
 });
 el('securityCallShip').addEventListener('click',()=>{
   const ship=Number(selected('securityContact'));
   if(contacts.has(ship) &&
      !(commsPenalty&&commsPenalty.until>status?.tick))
     void send('colonySecurityCall',{ship});
 });
 for(const [id,choice] of [
   ['securityIdentifyCaller','identify'],
   ['securityDeclineCaller','decline']
 ]){
   el(id).addEventListener('click',()=>{
     const call=inbox.get(selected('securityCallInbox'));
     if(call)void send('colonySecurityCallReply',{
       request:call.request,choice});
   });
 }
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
 return {parse,accept,reset,decodeHex,caseKey,nativeId};
});
