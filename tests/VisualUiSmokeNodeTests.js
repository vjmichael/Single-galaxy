'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
const base=path.join(__dirname,'../viewer');
const source=name=>fs.readFileSync(path.join(base,name),'utf8');
function setup() {
  const listeners={};const nodes={};
  const ctx=new Proxy({}, {get(target,p){if(p==='createRadialGradient')return()=>({addColorStop(){}});return (p in target)?target[p]:()=>{};},set(target,p,v){target[p]=v;return true;}});
  function node(id) {
    if(!nodes[id])nodes[id]={id,style:{},value:'',textContent:'',clientWidth:820,clientHeight:570,
      addEventListener:(name,fn)=>{listeners[id+':'+name]=fn;},
      getBoundingClientRect:()=>({width:820,height:570}),getContext:()=>ctx,
      replaceChildren(...children){this.children=children;}};
    return nodes[id];
  }
  const window={devicePixelRatio:1,addEventListener(){}};
  const document={getElementById:node,createElement:()=>({className:'',textContent:''}),addEventListener(){}};
  const sandbox={window,document,TextEncoder,console,requestAnimationFrame(){},Math,Number,BigInt};
  vm.createContext(sandbox);
  for(const file of ['contract.js','recordings.js','viewer.js']) {
    vm.runInContext(source(file),sandbox,{filename:file});
    if(file==='contract.js') sandbox.GalaxyContract=window.GalaxyContract;
  }
  return {nodes,listeners,node};
}
test('visual interface renders initial recorded snapshot without errors',()=>{
  const {node}=setup();
  assert.equal(node('tickPill').textContent,'Tick 0');
  assert.equal(node('asteroids').textContent,'10');
  assert.equal(node('cargo').textContent,'0 / 100');
  assert.equal(node('error').style.display,'none');
});
test('scrubbing to depleted final state updates HUD and event log',()=>{
  const {node,listeners}=setup();
  node('progress').value='10';listeners['progress:input']();
  assert.equal(node('asteroids').textContent,'0');
  assert.equal(node('cargo').textContent,'100 / 100');
  assert.equal(node('status').textContent,'HOLD FULL');
  assert.ok(node('log').children.some(e=>e.textContent.includes('ASTEROID DESPAWNED')));
});
test('hold-full scenario displays automatic stop at tick 3',()=>{
  const {node,listeners}=setup();
  node('scenario').value='hold';listeners['scenario:change']();
  node('progress').value='3';listeners['progress:input']();
  assert.equal(node('cargo').textContent,'10 / 10');
  assert.equal(node('ore').textContent,'10');
  assert.equal(node('toast').textContent,'MINING STOPPED · CARGO_FULL');
});
