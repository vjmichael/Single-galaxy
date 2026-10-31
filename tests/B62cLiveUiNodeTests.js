'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const ROOT=path.resolve(__dirname,'../viewer');
const raw=fs.readFileSync(path.resolve(__dirname,'../recordings/b62c_live_roundtrip.sgview'),'utf8');
const frames=raw.split(/(?=SGVIEW\|1\|)/).filter(x=>x.startsWith('SGVIEW|'));
function browser(){
  const nodes={},ctx=new Proxy({},{get(o,k){if(k==='createRadialGradient')return()=>({addColorStop(){}});return o[k]??(()=>{});},set(o,k,v){o[k]=v;return true;}});
  const el=id=>nodes[id]??(nodes[id]={style:{},textContent:'',value:'',clientWidth:900,clientHeight:580,
   addEventListener(){},getBoundingClientRect:()=>({width:900,height:580}),getContext:()=>ctx,
   replaceChildren(...items){this.children=items;}});
  const window={devicePixelRatio:1,addEventListener(){}};
  const document={getElementById:el,createElement:()=>({className:'',textContent:''}),addEventListener(){}};
  const scope={window,document,TextEncoder,console,requestAnimationFrame(){},Math,Number,BigInt};
  vm.createContext(scope);
  for(const name of ['contract.js','recordings.js','viewer.js']){
    vm.runInContext(fs.readFileSync(path.join(ROOT,name),'utf8'),scope,{filename:name});
    if(name==='contract.js')scope.GalaxyContract=window.GalaxyContract;
  }
  return {window,el};
}
test('B6.2c accepts destination snapshots and displays protective cloak, local market, and cargo',()=>{
  const {window,el}=browser();let resets=[];
  window.GalaxySurveyBridge={systemChanged:x=>resets.push(x)};
  window.GalaxyLiveBridge.reset();let foundArrival=false,foundJumping=false;
  for(const frame of frames){
    window.GalaxyLiveBridge.accept(frame);
    if(el('jumpState').textContent.startsWith('JUMPING'))foundJumping=true;
    if(el('currentSystem').textContent==='2' && el('jumpState').textContent.startsWith('ARRIVAL CLOAK')){
      foundArrival=true;
      assert.equal(el('marketPrice').textContent,'7 credits / ore');
      assert.equal(el('status').textContent.startsWith('ARRIVAL CLOAK'),true);
    }
  }
  assert(foundArrival && foundJumping,'jump phase not presented to user');
  assert.deepEqual(resets,[2,1]);
  assert.equal(el('walletCredits').textContent,'140');
  assert.equal(el('currentSystem').textContent,'1');
  assert.equal(el('error').style.display,'none');
});
test('B6.2c rejects unannounced second snapshot or a rewinding snapshot',()=>{
  const {window}=browser();window.GalaxyLiveBridge.reset();
  window.GalaxyLiveBridge.accept(frames[0]);
  assert.throws(()=>window.GalaxyLiveBridge.accept(frames[0]),/Unexpected or stale/);
  window.GalaxyLiveBridge.reset();window.GalaxyLiveBridge.accept(frames[0]);
  const frontier=frames.find(f=>/^SGVIEW\|1\|1\|[^\n]*\|2\|/.test(f));
  assert(frontier);
  assert.throws(()=>window.GalaxyLiveBridge.accept(frontier),/Unexpected or stale/);
});
