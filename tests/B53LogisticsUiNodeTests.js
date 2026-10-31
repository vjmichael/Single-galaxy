'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const ROOT=path.join(__dirname,'../viewer');
function createUi(){
 const nodes={},ctx=new Proxy({}, {get(obj,k){if(k==='createRadialGradient')return()=>({addColorStop(){}});return obj[k]??(()=>{});},set(obj,k,v){obj[k]=v;return true;}});
 const el=id=>nodes[id]??(nodes[id]={style:{},textContent:'',value:'',clientWidth:900,clientHeight:580,
  addEventListener(){},getBoundingClientRect:()=>({width:900,height:580}),getContext:()=>ctx,
  replaceChildren(...items){this.children=items;}});
 const window={devicePixelRatio:1,addEventListener(){}};
 const document={getElementById:el,createElement:()=>({className:'',textContent:''}),addEventListener(){}};
 const sandbox={window,document,TextEncoder,console,requestAnimationFrame(){},Math,Number,BigInt};
 vm.createContext(sandbox);
 for(const name of ['contract.js','recordings.js','viewer.js']){
  vm.runInContext(fs.readFileSync(path.join(ROOT,name),'utf8'),sandbox,{filename:name});
  if(name==='contract.js')sandbox.GalaxyContract=window.GalaxyContract;
 }
 return {window,el};
}
const demo=fs.readFileSync(path.join(__dirname,'../recordings/b53_station_trade.sgview'),'utf8')
 .split(/(?=SGVIEW\|1\|)/).filter(s=>s.startsWith('SGVIEW|'));
test('B5.3 UI shows committed Station B sale, player wallet and market funding',()=>{
 const {window,el}=createUi();window.GalaxyLiveBridge.reset();
 for(const frame of demo)window.GalaxyLiveBridge.accept(frame);
 assert.equal(el('dockState').textContent,'DOCKED');
 assert.equal(el('walletCredits').textContent,'100');
 assert.equal(el('marketPrice').textContent,'5 credits / ore');
 assert.equal(el('marketStock').textContent,'20 ore · 900 credits');
 assert.equal(el('hangarOre').textContent,'0 / 1000');
 assert.equal(el('cargo').textContent,'0 / 20');
 assert.equal(el('error').style.display,'none');
});
