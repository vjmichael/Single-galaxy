'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const ROOT=path.join(__dirname,'../viewer');
function make(){
 const nodes={},listeners={},ctx=new Proxy({}, {get(o,p){if(p==='createRadialGradient')return()=>({addColorStop(){}});return o[p]??(()=>{});},set(o,p,v){o[p]=v;return true;}});
 const el=id=>nodes[id]??(nodes[id]={style:{},textContent:'',value:'',clientWidth:900,clientHeight:580,
  addEventListener:(name,fn)=>listeners[id+':'+name]=fn,getBoundingClientRect:()=>({width:900,height:580}),getContext:()=>ctx,
  replaceChildren(...items){this.children=items;}});
 const window={devicePixelRatio:1,addEventListener(){}};
 const document={getElementById:el,createElement:()=>({className:'',textContent:''}),addEventListener(){}};
 const sandbox={window,document,TextEncoder,console,requestAnimationFrame(){},Math,Number,BigInt};
 vm.createContext(sandbox);
 for(const fn of ['contract.js','recordings.js','viewer.js']){
  vm.runInContext(fs.readFileSync(path.join(ROOT,fn),'utf8'),sandbox,{filename:fn});
  if(fn==='contract.js')sandbox.GalaxyContract=window.GalaxyContract;
 }
 return {window,el};
}
const demo=fs.readFileSync(path.join(__dirname,'../recordings/b52_station_demo.sgview'),'utf8')
  .split(/(?=SGVIEW\|1\|)/).filter(x=>x.startsWith('SGVIEW|'));
test('B5.2 HUD shows real initial docked ship and empty local hangar',()=>{
 const {window,el}=make();window.GalaxyLiveBridge.reset();window.GalaxyLiveBridge.accept(demo[0]);
 assert.equal(el('dockState').textContent,'DOCKED');
 assert.equal(el('hangarOre').textContent,'0 / 100');
 assert.equal(el('cargo').textContent,'0 / 20');
});
test('B5.2 HUD shows final docked 20 ore hangar and empty cargo after verified frames',()=>{
 const {window,el}=make();window.GalaxyLiveBridge.reset();
 for(const frame of demo)window.GalaxyLiveBridge.accept(frame);
 assert.equal(el('dockState').textContent,'DOCKED');
 assert.equal(el('hangarOre').textContent,'20 / 100');
 assert.equal(el('cargo').textContent,'0 / 20');
 assert.ok(el('log').children.some(e=>e.textContent.includes('20 ore transferred')));
 assert.equal(el('error').style.display,'none');
});
