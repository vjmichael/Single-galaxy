#!/usr/bin/env node
// ECON-02B2d4c2 Linux developer host: same-origin, loopback-only, authenticated
// controls for a persistent world. Never deploy as an Internet-facing server.
'use strict';
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawn } = require('node:child_process');
const root = path.resolve(__dirname, '..');
const www = path.join(root, 'viewer');
const args = process.argv.slice(2);
function option(name, defaultValue) {
  const i = args.indexOf(name);
  if (i < 0) return defaultValue;
  if (i === args.length - 1) throw Error(`Missing argument after ${name}`);
  return args[i + 1];
}
const defaultBridge = [path.join(root,'build','galaxy_factory_host_bridge'),path.join(root,'..','build','galaxy_factory_host_bridge')].find(p=>fs.existsSync(p)) || path.join(root,'build','galaxy_factory_host_bridge');
const bridge = path.resolve(option('--bridge', defaultBridge));
const database = option('--db', '');
const site = option('--site', '');
if (!database) { console.error('Usage: node scripts/factory_live_server.js --db EXISTING.sqlite [--bridge path] [--site id]'); process.exit(2); }
if (!fs.existsSync(database) || !fs.statSync(database).isFile() || fs.statSync(database).size === 0) {
  console.error('Refusing to start: a nonempty persistent-world SQLite file is required.');
  process.exit(2);
}
const child = spawn(bridge, [path.resolve(database), ...(site ? ['--site', site] : [])], { stdio: ['pipe','pipe','inherit'], shell: false });
let closed = false, buffer = '', outstanding = [];
child.stdout.setEncoding('utf8');
child.stdout.on('data', chunk => {
  buffer += chunk;
  if (buffer.length > 2000000) { child.kill(); return; }
  while (buffer.includes('\n')) {
    const index = buffer.indexOf('\n'); const line = buffer.slice(0,index); buffer = buffer.slice(index+1);
    const req = outstanding.shift();
    if (!req) { child.kill(); return; }
    clearTimeout(req.timer);
    try { req.resolve(JSON.parse(line)); } catch { req.reject(Error('Invalid native host reply')); }
  }
});
function failPending(error) { for (const x of outstanding.splice(0)) { clearTimeout(x.timer); x.reject(error); } }
child.on('error', error => { closed=true; failPending(error); });
child.on('exit', () => { closed=true; failPending(Error('Native colony host exited')); });
function invoke(command) {
  if (closed || !child.stdin.writable) return Promise.reject(Error('Native colony host unavailable'));
  if (command.includes('\n') || command.includes('\r') || command.length>1401000) return Promise.reject(Error('Invalid host command'));
  return new Promise((resolve,reject) => {
    const request = { resolve, reject, timer: null };
    request.timer = setTimeout(() => { closed=true; child.kill(); failPending(Error('Native host response timeout')); }, 15000);
    outstanding.push(request);
    child.stdin.write(command+'\n', error => { if (error) { closed=true; child.kill(); failPending(error); } });
  });
}
const secret = crypto.randomBytes(32).toString('hex');
const prefix = `/session/${secret}/`;
const files = new Map([['factory.html','text/html; charset=utf-8'],['factory.js','text/javascript; charset=utf-8'],
  ['factory-layout.js','text/javascript; charset=utf-8'],['factory-live.js','text/javascript; charset=utf-8']]);
let origin = '';
const server = http.createServer(async (req,res) => {
  res.setHeader('Cache-Control','no-store'); res.setHeader('Referrer-Policy','no-referrer');
  res.setHeader('X-Content-Type-Options','nosniff'); res.setHeader('X-Frame-Options','DENY');
  res.setHeader('Content-Security-Policy',"default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; connect-src 'self'; img-src 'self' blob:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'none'");
  const send = (code,data) => { res.statusCode=code;res.setHeader('Content-Type','application/json; charset=utf-8');res.end(JSON.stringify(data)); };
  if (req.headers.host !== origin.slice('http://'.length)) return send(403,{ok:false,error:'Invalid host'});
  const uri = (req.url || '').split('?')[0];
  if (!uri.startsWith(prefix)) return send(404,{ok:false,error:'Unknown session'});
  const endpoint = uri.slice(prefix.length);
  if(req.method==='GET' && files.has(endpoint)) {
    const file = path.join(www, endpoint);
    try { res.setHeader('Content-Type',files.get(endpoint));res.end(fs.readFileSync(file)); }
    catch { send(500,{ok:false,error:'Editor asset not available'}); }
    return;
  }
  if(req.method==='GET' && endpoint==='api/hello'){
    try{return send(200,await invoke('HELLO'));}catch(e){return send(503,{ok:false,error:e.message});}
  }
  if(req.method==='GET' && endpoint==='api/plans'){
    try{return send(200,await invoke('LIST'));}catch(e){return send(503,{ok:false,error:e.message});}
  }
  if(req.method!=='POST' || !['register','step','cancel','status'].includes(endpoint.replace('api/','')) || !endpoint.startsWith('api/'))
    return send(404,{ok:false,error:'Unknown route'});
  // Exact same-origin and explicit per-session header stop browser CSRF, file://,
  // and foreign-site writes. The launch operator, NOT the request, owns identity.
  if(req.headers.origin!==origin || req.headers['x-sg-session']!==secret ||
     (req.headers['sec-fetch-site'] && req.headers['sec-fetch-site']!=='same-origin'))
    return send(403,{ok:false,error:'Session authorization denied'});
  if(!String(req.headers['content-type']||'').startsWith('application/json'))return send(415,{ok:false,error:'JSON required'});
  let body='';
  try {
    for await(const chunk of req) { body+=chunk; if(body.length>1100000)throw Error('Request too large'); }
    const input=JSON.parse(body);
    if(!input || typeof input!=='object' || Array.isArray(input))throw Error('Invalid request');
    const method=endpoint.slice(4);
    let command;
    if(method==='register'){
      if(typeof input.layout!=='string' || Buffer.byteLength(input.layout)>1048576)throw Error('Invalid blueprint');
      command='REGISTER|'+Buffer.from(input.layout,'utf8').toString('base64');
    }else{
      if(typeof input.id!=='number' || !Number.isSafeInteger(input.id) || input.id<1)throw Error('Invalid plan ID');
      command={step:'STEP',cancel:'CANCEL',status:'STATUS'}[method]+'|'+input.id;
    }
    const answer=await invoke(command);
    return send(answer.ok?200:400,answer);
  }catch(error){return send(400,{ok:false,error:String(error.message||error).slice(0,256)});}
});
function stop(){ server.close(); child.kill(); }
for(const sig of ['SIGTERM','SIGINT'])process.on(sig,()=>{stop();process.exit(0);});
server.listen(0,'127.0.0.1', async () => {
  origin=`http://127.0.0.1:${server.address().port}`;
  try { const intro=await invoke('HELLO');if(!intro.ok)throw Error(intro.error);
    console.log(`SG_FACTORY_URL=${origin}${prefix}factory.html`);
    console.log(`FACTORY_SITE=${intro.site} SHIP=${intro.ship} AVAILABLE_MODULES=${intro.modulesAvailable}`);
  } catch (e) { console.error('Native colony host startup failed:',e.message);stop();process.exitCode=1; }
});
