#!/usr/bin/env node
'use strict';
// DEMOG-05q6e5b8j: localhost-only authenticated player-session endpoint.
// A credential verified by this host resolves to one server-held PlayerId;
// native status reads are made with that ID over a private child process pipe.
// Never run this endpoint behind a public reverse proxy or on a public IP.
const fs=require('node:fs');
const path=require('node:path');
const http=require('node:http');
const crypto=require('node:crypto');
const {spawn}=require('node:child_process');
const {loadAccounts,verify}=require('./shipping_auth');
const root=path.resolve(__dirname,'..');
const maxSessions=64,ttl=20*60*1000,failedWindow=5*60*1000;
const SAFE_ID=/^[a-zA-Z0-9_.-]{1,100}$/;
const OPERATIONS={approve:'APPROVE',revoke:'REVOKE',dispatch:'DISPATCH',load:'LOAD',unload:'UNLOAD',ack:'ACK',request_return:'REQUEST_RETURN',approve_return:'APPROVE_RETURN',return:'RETURN',approve_redirect:'APPROVE_REDIRECT',deliver_redirect:'DELIVER_REDIRECT',ack_redirect:'ACK_REDIRECT'};
function parseArgs(args){const get=(x,d='')=>{const i=args.indexOf(x);return i<0?d:args[i+1]||'';};
 return {db:get('--db'),accounts:get('--accounts'),bridge:get('--bridge',path.join(root,'build','galaxy_shipping_status_bridge'+(process.platform==='win32'?'.exe':''))),port:Number(get('--port','0')),
   writerSeed:get('--writer-seed'),region:get('--region'),colony:get('--colony'),families:get('--families')};}
function createServer({database,accounts,bridge,port=0,writer=null}) {
 if(!fs.statSync(database).isFile()||fs.statSync(database).size===0)throw Error('World database missing');
 if(!Number.isInteger(port)||port<0||port>65535)throw Error('Invalid local port');
 const sessions=new Map(),attempts=new Map();let closed=false,origin='',lastError='';
 if(writer&&(!Number.isSafeInteger(writer.seed)||writer.seed<1||![writer.region,writer.colony].every(x=>SAFE_ID.test(x))||!Array.isArray(writer.families)||!writer.families.length||!writer.families.every(x=>SAFE_ID.test(x))))throw Error('Invalid writer configuration');
 const child=spawn(bridge,[path.resolve(database),...(writer?['--writer',String(writer.seed),writer.region,writer.colony,writer.families.join(',')]:[])],{stdio:['pipe','pipe','inherit'],shell:false});
 let buffer='',pending=[];
 child.stdout.setEncoding('utf8');
 function fail(error){closed=true;lastError=String(error.message||error);while(pending.length){const p=pending.shift();clearTimeout(p.timer);p.reject(error);}}
 child.stdout.on('data',c=>{
  buffer+=c;if(buffer.length>1024*1024){child.kill();fail(Error('Oversized native reply'));return;}
  while(buffer.includes('\n')){const i=buffer.indexOf('\n'),line=buffer.slice(0,i);buffer=buffer.slice(i+1);
   const req=pending.shift();if(!req){child.kill();fail(Error('Unsolicited native reply'));return;}
   clearTimeout(req.timer);try{req.resolve(JSON.parse(line));}catch{req.reject(Error('Invalid native reply'));}
  }
 });
 child.on('error',fail);child.on('exit',()=>fail(Error('Native shipping reader exited')));
 function invoke(command){if(closed||!child.stdin.writable)return Promise.reject(Error('Native shipping host offline'));
  if(command.length>650||command.includes('\n')||command.includes('\r'))return Promise.reject(Error('Invalid native command'));
  return new Promise((resolve,reject)=>{
   const item={resolve,reject,timer:setTimeout(()=>{child.kill();fail(Error('Native status timeout'));},10000)};
   pending.push(item);child.stdin.write(command+'\n',e=>{if(e){child.kill();fail(e);}});
  });
 }
 function session(req){
  const cookie=String(req.headers.cookie||'').split(';').map(x=>x.trim()).find(x=>x.startsWith('sgsid='));
  const id=cookie?.slice(6);if(!id||!/^[0-9a-f]{64}$/.test(id))return null;
  const row=sessions.get(id);if(!row)return null;
  if(row.expires<Date.now()){sessions.delete(id);return null;}
  return {...row,id};
 }
 const server=http.createServer(async(req,res)=>{
  res.setHeader('Cache-Control','no-store');res.setHeader('Referrer-Policy','no-referrer');
  res.setHeader('X-Content-Type-Options','nosniff');res.setHeader('X-Frame-Options','DENY');
  res.setHeader('Content-Security-Policy',"default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'");
  const send=(code,data)=>{res.statusCode=code;res.setHeader('Content-Type','application/json; charset=utf-8');res.end(JSON.stringify(data));};
  if(req.headers.host!==origin.slice(7))return send(403,{ok:false,error:'Host refused'});
  const url=(req.url||'').split('?')[0];
  if(req.method==='GET'&&(url==='/'||url==='/shipping.html'||url==='/shipping.js')){
   try{const name=url==='/shipping.js'?'shipping.js':'shipping.html';
    res.setHeader('Content-Type',name.endsWith('.js')?'text/javascript; charset=utf-8':'text/html; charset=utf-8');
    res.end(fs.readFileSync(path.join(root,'viewer',name)));}catch{return send(503,{ok:false,error:'Viewer unavailable'});}return;
  }
  const principal=session(req);
  if(req.method==='GET'&&url==='/api/session')return principal?send(200,{ok:true,username:principal.username,playerId:principal.playerId,csrf:principal.csrf,writes:!!writer}):send(401,{ok:false,error:'Login required'});
  if(req.method==='GET'&&url==='/api/shipments'){
   if(!principal)return send(401,{ok:false,error:'Login required'});
   try{const answer=await invoke('LIST|'+principal.playerId);return send(answer.ok?200:503,answer);}
   catch{return send(503,{ok:false,error:'Status service unavailable'});}
  }
  const action=url.startsWith('/api/shipment/')?url.slice('/api/shipment/'.length):'';
  if(req.method!=='POST'||(url!=='/api/login'&&url!=='/api/logout'&&(!writer||!Object.prototype.hasOwnProperty.call(OPERATIONS,action))))return send(404,{ok:false,error:'Unknown route'});
  if(req.headers.origin!==origin||req.headers['sec-fetch-site']&&req.headers['sec-fetch-site']!=='same-origin')return send(403,{ok:false,error:'Origin denied'});
  if(req.headers['content-type']!=='application/json')return send(415,{ok:false,error:'JSON required'});
  if((url==='/api/logout'||action)&&(!principal||req.headers['x-sg-csrf']!==principal.csrf))return send(403,{ok:false,error:'Session denied'});
  let body='';try{for await(const chunk of req){body+=chunk;if(Buffer.byteLength(body)>4096)throw Error('Too large');}
   const data=JSON.parse(body);if(!data||Array.isArray(data)||typeof data!=='object')throw Error('Invalid body');
   if(action){
    if(!SAFE_ID.test(data.id||''))return send(400,{ok:false,error:'Invalid shipment identifier'});
    const requiresRole=['approve','ack','approve_return','approve_redirect','ack_redirect'].includes(action),requiresCommand=action==='request_return',requiresDispatch=action==='dispatch';
    const allowed=new Set(['id',...(requiresRole?['role']:[]),...(requiresCommand?['returnCommand']:[]),...(requiresDispatch?['dispatchId']:[])]);
    if(Object.keys(data).some(k=>!allowed.has(k))||(requiresRole&&(!Number.isInteger(data.role)||data.role<1||data.role>4))||
       (requiresCommand&&(!Number.isSafeInteger(data.returnCommand)||data.returnCommand<1))||(requiresDispatch&&!SAFE_ID.test(data.dispatchId||'')))
       return send(400,{ok:false,error:'Invalid freight operation'});
    const args=[OPERATIONS[action],String(principal.playerId),data.id];
    if(requiresRole)args.push(String(data.role));if(requiresCommand)args.push(String(data.returnCommand));if(requiresDispatch)args.push(data.dispatchId);
    try{const answer=await invoke(args.join('|'));return send(answer.ok?200:403,answer);}
    catch{return send(503,{ok:false,error:'Canonical host unavailable'});}
   }
   if(url==='/api/logout'){
    sessions.delete(principal.id);res.setHeader('Set-Cookie','sgsid=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0');
    return send(200,{ok:true});
   }
   if(typeof data.username!=='string'||typeof data.password!=='string'||Object.keys(data).some(k=>!['username','password'].includes(k)))return send(400,{ok:false,error:'Invalid login'});
   const key='loopback';const now=Date.now(),hits=(attempts.get(key)||[]).filter(t=>now-t<failedWindow);
   if(hits.length>=8)return send(429,{ok:false,error:'Too many attempts'});
   const matched=await verify(accounts,data.username,data.password);
   if(!matched){hits.push(now);attempts.set(key,hits);return send(401,{ok:false,error:'Invalid credentials'});}
   attempts.delete(key);
   for(const [id,s] of sessions)if(s.expires<now)sessions.delete(id);
   if(sessions.size>=maxSessions)return send(503,{ok:false,error:'Session capacity reached'});
   const id=crypto.randomBytes(32).toString('hex'),csrf=crypto.randomBytes(32).toString('hex');
   sessions.set(id,{playerId:matched.playerId,username:matched.username,csrf,expires:now+ttl});
   res.setHeader('Set-Cookie',`sgsid=${id}; HttpOnly; SameSite=Strict; Path=/; Max-Age=${Math.floor(ttl/1000)}`);
   return send(200,{ok:true,username:matched.username,playerId:matched.playerId,csrf,writes:!!writer});
  }catch{return send(400,{ok:false,error:'Invalid request'});}
 });
 return {server,child,listen:async()=>{
   await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(port,'127.0.0.1',()=>{
    origin='http://127.0.0.1:'+server.address().port;resolve();});});
   try{
    const ready=await invoke('CAPS');
    if(!ready.ok||ready.writes!==!!writer)throw Error('Native shipping authority configuration mismatch');
    return origin;
   }catch(e){child.kill();await new Promise(resolve=>server.close(resolve));throw e;}
  },stop:()=>new Promise(resolve=>server.close(()=>{child.kill();resolve();})),get error(){return lastError;}};
}
if(require.main===module){try{
 const a=parseArgs(process.argv.slice(2));if(!a.db||!a.accounts)throw Error('Usage: node scripts/shipping_live_server.js --db WORLD.sqlite --accounts PRIVATE.json [--bridge EXECUTABLE]');
 const writer=a.writerSeed?{seed:Number(a.writerSeed),region:a.region,colony:a.colony,families:a.families.split(',')}:null;
 const live=createServer({database:a.db,accounts:loadAccounts(a.accounts),bridge:path.resolve(a.bridge),port:a.port,writer});
 live.listen().then(url=>{console.log('SG_SHIPPING_URL='+url+'/shipping.html');console.log('Loopback-only authenticated shipment status; not a public multiplayer host');}).catch(e=>{console.error(e.message);process.exitCode=1;});
 for(const sig of ['SIGTERM','SIGINT'])process.on(sig,()=>live.stop().then(()=>process.exit(0)));
 }catch(e){console.error('Shipping server startup refused:',e.message);process.exitCode=2;}
}
module.exports={createServer,parseArgs};
