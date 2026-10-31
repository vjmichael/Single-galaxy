'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const base=path.join(__dirname,'../viewer');
function setup(){
 const listeners={},nodes={};
 const ctx=new Proxy({}, {get(target,p){if(p==='createRadialGradient')return()=>({addColorStop(){}});return target[p]??(()=>{});},set(target,p,v){target[p]=v;return true;}});
 function el(id){if(!nodes[id])nodes[id]={style:{},value:'',textContent:'',clientWidth:800,clientHeight:550,
     addEventListener:(name,fn)=>{listeners[id+':'+name]=fn;},
     getBoundingClientRect:()=>({width:800,height:550}),getContext:()=>ctx,
     replaceChildren(...items){this.children=items;}};return nodes[id];}
 const window={devicePixelRatio:1,addEventListener(){}};
 const document={getElementById:el,createElement:()=>({className:'',textContent:''}),addEventListener(){}};
 const sandbox={window,document,TextEncoder,console,requestAnimationFrame(){},Math,Number,BigInt};
 vm.createContext(sandbox);
 for(const file of ['contract.js','recordings.js','viewer.js']){
   vm.runInContext(fs.readFileSync(path.join(base,file),'utf8'),sandbox,{filename:file});
   if(file==='contract.js') sandbox.GalaxyContract=window.GalaxyContract;
 }
 return {window,el};
}
function frames(name){
 const raw=fs.readFileSync(path.join(__dirname,'..',name+'.live.sgview'),'utf8');
 return raw.split(/(?=SGVIEW\|1\|)/).filter(s=>s.startsWith('SGVIEW|'));
}
test('live frames update HUD and cargo-full notification as C++ world commits',()=>{
 const {window,el}=setup();el('scenario').value='hold';
 const b=window.GalaxyLiveBridge;b.reset();
 for(const raw of frames('hold')) b.accept(raw);
 assert.equal(el('tickPill').textContent,'Tick 7');
 assert.equal(el('cargo').textContent,'10 / 10');
 assert.equal(el('ore').textContent,'10');
 assert.equal(el('status').textContent,'HOLD FULL');
 assert.equal(el('sourcebadge').textContent,'LIVE · SQLite committed');
 assert.ok(el('log').children.some(e=>e.textContent.includes('CARGO_FULL')));
});
test('live frame gap is rejected without updating displayed world',()=>{
 const {window,el}=setup();el('scenario').value='hold';
 const b=window.GalaxyLiveBridge;b.reset();
 const items=frames('hold');b.accept(items[0]);b.accept(items[1]);
 const prev=el('cargo').textContent;
 assert.throws(()=>b.accept(items[3]),/Missing or out-of-order delta/);
 assert.equal(el('cargo').textContent,prev);
 assert.equal(el('tickPill').textContent,'Tick 1');
});
test('live client shows depleted rocks disappearing',()=>{
 const {window,el}=setup();el('scenario').value='ten';
 const b=window.GalaxyLiveBridge;b.reset();
 for(const raw of frames('ten'))b.accept(raw);
 assert.equal(el('asteroids').textContent,'0');
 assert.equal(el('cargo').textContent,'100 / 100');
});
