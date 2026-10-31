/* B6.1 server-origin scan events: validated, read-only display. SGVIEW stays v1. */
(function(root,factory){
 const api=factory();if(typeof module!=='undefined'&&module.exports)module.exports=api;
 root.GalaxySurveyContract=api;
})(typeof window!=='undefined'?window:globalThis,function(){
 'use strict';
 const kinds=['Star','Planet','Moon','Station','Ship','Asteroid deposit','Gas cloud','Anomaly','Wormhole','Structure'];
 function integer(value,signed=false){
   if(!(signed?/^(?:0|-?[1-9]\d*)$/:/^(?:0|[1-9]\d*)$/).test(value))throw Error('Noncanonical sensor integer');
   const n=Number(value);if(!Number.isSafeInteger(n))throw Error('Sensor integer overflow');return n;
 }
 function decode(line){
   if(typeof line!=='string'||line.length>180||!/^SURVEY\|1\|/.test(line)||!/^[-A-Z0-9_|]+$/.test(line))throw Error('Invalid sensor message');
   const a=line.split('|');if(a.length<5)throw Error('Truncated sensor message');
   const tick=integer(a[2]),type=a[3],v=a.slice(4);
   let payload={tick,type};
   if(type==='SYSTEM'){if(v.length!==1)throw Error('Bad sensor system');payload.system=integer(v[0]);if(!payload.system)throw Error('Invalid sensor system');}
   else if(type==='KNOWN'){
     if(v.length!==4)throw Error('Bad known object');
     const [site,kind,x,y]=v.map((x,i)=>integer(x,i>1));
     if(!site||kind>9)throw Error('Invalid known object');
     Object.assign(payload,{site,kind,x,y});
   }else if(type==='D'){
     if(v.length!==4)throw Error('Bad directional contact');
     const [site,kind,bearing,range]=v.map(x=>integer(x));
     if(!site||kind>9||bearing>=360||range>2000000000)throw Error('Invalid directional contact');
     Object.assign(payload,{site,kind,bearing,range});
   }else if(type==='P'||type==='ARCHIVE'||type==='HISTORY'){
     if(v.length!==(type==='P'?5:7))throw Error('Bad survey result');
     const [site,kind,x,y,confidence]=v.slice(0,5).map((x,i)=>integer(x,i===2||i===3));
     if(!site||kind>9||confidence<1||confidence>100)throw Error('Invalid survey result');
     Object.assign(payload,{site,kind,x,y,confidence});
     if(type==='ARCHIVE'||type==='HISTORY'){
       payload.observed=integer(v[5]);payload.published=integer(v[6]);
       if(!payload.observed||payload.observed>tick||payload.published>1)throw Error('Bad observation timestamp or publication status');
     }
   }else if(type==='ROUTE'||type==='REROUTE'){
     if(v.length!==4)throw Error('Bad route');
     const dest=integer(v[0]),policy=v[1],cost=integer(v[2]);
     if(!dest||!['S','Q'].includes(policy)||cost>100000000)throw Error('Invalid route');
     const path=v[3].split('-').map(part=>integer(part));
     if(path.length<1||path.length>32||path.some(id=>id<1)||path[path.length-1]!==dest||new Set(path).size!==path.length)throw Error('Invalid route path');
     Object.assign(payload,{dest,policy,cost,path});
   }else if(type==='ROUTE_NONE'){
     if(v.length!==2)throw Error('Bad no-route');
     payload.dest=integer(v[0]);payload.policy=v[1];
     if(!payload.dest||!['S','Q'].includes(payload.policy))throw Error('Invalid no-route');
   }else if(type==='GATE_STATUS'){
     if(v.length!==7)throw Error('Bad local gate operations record');
     const [from,to,id,condition,technicians,parts,online]=v.map(x=>integer(x));
     if(!from||from>7||!to||to>7||from===to||!id||condition>1000||
        technicians>16||parts>1000000||online>1||(condition===0&&online!==0))
       throw Error('Invalid gate operations');
     Object.assign(payload,{from,to,id,condition,technicians,parts,online:!!online});
   }else if(type==='QA_GATE'){
     if(v.length!==3)throw Error('Bad QA gate record');
     const [from,to,online]=v.map(x=>integer(x));
     if(from<1||from>7||to<1||to>7||from===to||online>1)throw Error('Invalid QA gate');
     Object.assign(payload,{from,to,online:!!online});
   }else if(type==='QA_MODE'||type==='QA_DONE'||type==='QA_STARTED'||type==='QA_STOPPED'){
     if(v.length!==1)throw Error('Bad QA status');
     const value=integer(v[0]);if(value>7)throw Error('QA status out of range');
     payload.value=value;
   }else if(type==='LEG'){
     if(v.length!==2)throw Error('Bad route leg');
     const [next,gate]=v.map(x=>integer(x));
     if(next>7 || (next===0)!==(gate===0))throw Error('Invalid route leg');
     Object.assign(payload,{next,gate});
   }else if(type==='DEPLOY'){
     if(v.length!==3)throw Error('Bad deployment');
     const [id,x,y]=v.map((s,i)=>integer(s,i>0));
     if(!id)throw Error('Invalid probe');Object.assign(payload,{id,x,y});
   }else if(['D_DONE','P_DONE','RECOVER','PUBLISH','ARCHIVE_DONE','HISTORY_DONE'].includes(type)){
     if(v.length!==1)throw Error('Bad result count');payload.count=integer(v[0]);
   }else if(type==='ERROR'){
     if(v.length!==1||!['NOT_IN_SPACE','NO_FREE_PROBES','NO_ROUTE_LEG','QA_DISABLED','QA_NO_ROUTE','INVALID_GATE'].includes(v[0]))throw Error('Bad sensor error');
     payload.reason=v[0];
   }else throw Error('Unknown sensor record');
   return payload;
 }
 function label(event){
   const kind=(kinds[event.kind]||'Object');
   switch(event.type){
     case 'SYSTEM':return `Entered system ${event.system}; refreshing local survey`;
     case 'KNOWN':return `Mapped on entry: ${kind} #${event.site} at (${event.x}, ${event.y})`;
     case 'D':return `${kind} #${event.site} · bearing ${event.bearing}° · ~${event.range} mm`;
     case 'P':return `${kind} #${event.site} · estimated (${event.x}, ${event.y}) · ${event.confidence}% confidence`;
     case 'HISTORY':return `${kind} #${event.site} · ${event.confidence}% · measured tick ${event.observed} · ${event.published?'public':'private'} (historical)`;
     case 'ARCHIVE':return `${kind} #${event.site} · ${event.confidence}% · observed tick ${event.observed} · ${event.published?'public':'private'}`;
     case 'REROUTE':return `Route revised after gate disruption: ${event.path.join(' → ')} · cost ${event.cost}`;
     case 'ROUTE':return `Route to system ${event.dest} · ${event.policy==='S'?'shortest':'safer'} · ${event.path.join(' → ')} · cost ${event.cost}`;
     case 'GATE_STATUS':return `Local gate ${event.from} → ${event.to}: ${event.online?'online':'offline'} · condition ${event.condition}/1000 · ${event.technicians} technicians · ${event.parts} repair parts`;
     case 'QA_GATE':return `TEST gate ${event.from} ↔ ${event.to}: ${event.online?'restored':'offline'}`;
     case 'QA_MODE':return 'QA testing mode enabled';
     case 'QA_STARTED':return `QA route automation started toward system ${event.value}`;
     case 'QA_STOPPED':return 'QA automation stopped';
     case 'QA_DONE':return `QA route completed in system ${event.value}`;
     case 'LEG':return event.next?`Next leg: system ${event.next} (gate #${event.gate})`:'Route completed or none selected';
     case 'ROUTE_NONE':return `No operational route to system ${event.dest}`;
     case 'DEPLOY':return `Probe ${event.id} deployed at (${event.x}, ${event.y})`;
     case 'RECOVER':return `${event.count} probe(s) recovered`;
     case 'PUBLISH':return `${event.count} private observation(s) published`;
     case 'ERROR':return `Rejected: ${event.reason}`;
     case 'D_DONE':return `Directional scan complete · ${event.count} contact(s)`;
     case 'P_DONE':return `Probe scan complete · ${event.count} hidden site(s)`;
     case 'HISTORY_DONE':return `Survey history · ${event.count} revision(s)`;
     case 'ARCHIVE_DONE':return `Survey archive · ${event.count} record(s)`;
   }
 }
 return {decode,label};
});
(function(){'use strict';
 if(typeof document==='undefined'||!document.getElementById('surveyResults'))return;
 const results=document.getElementById('surveyResults'),status=document.getElementById('surveyStatus');
 const seen=new Set();let lastTick=0;
 window.GalaxySurveyBridge={
   reset(){seen.clear();lastTick=0;results.replaceChildren();window.GalaxyRegionView?.reset();status.textContent='Local display resets on reconnect; history remains archived.';},
   systemChanged(system){seen.clear();results.replaceChildren();status.textContent='System '+system+': new local sensor view. Prior observations remain in the SQLite archive.';const advisory=document.getElementById('routeResult');if(advisory)advisory.textContent='System changed. Request a new route before travel.';},
   accept(raw){
     const event=window.GalaxySurveyContract.decode(raw);
     if(seen.has(raw))return event;
     if(event.tick<lastTick)throw Error('Sensor tick regression');
     lastTick=event.tick;seen.add(raw);
     if(event.type==='SYSTEM')this.systemChanged(event.system);
     window.GalaxyRegionView?.onEvent(event);
     if(event.type==='ROUTE'||event.type==='REROUTE'||event.type==='ROUTE_NONE'||event.type==='LEG') {
       const advisory=document.getElementById('routeResult');
       if(advisory)advisory.textContent=window.GalaxySurveyContract.label(event)+' · advisory; jump manually';
     }
     const item=document.createElement('div');item.className='entry';
     item.textContent=`Tick ${event.tick}: ${window.GalaxySurveyContract.label(event)}`;
     results.prepend(item);
     while(results.children.length>30)results.removeChild(results.lastChild);
     status.textContent=`Latest sensor event: ${window.GalaxySurveyContract.label(event)}`;
     return event;
   }
 };
})();
