'use strict';
// SHIP-STATUS-05: native-telemetry contract for the actual player cockpit.
// Node-only; no DOM, game server, network, player credentials or fake results.
const assert=require('node:assert/strict');
const cyber=require('../viewer/cyber-controls.js');
const status=cyber.parse('COLONY|1|5|CYBER_STATUS|17|1|0|3|1');
assert.equal(status.ship,17);
assert.equal(status.emitter,true);
assert.equal(status.firewall,false);
assert.equal(status.cooldown,3);
assert.equal(status.colony,true);
// Fitted is not enough: a real emitter has to be effective and non-isolated
// on the current committed tick before the cockpit can request an attack.
assert.equal(cyber.canAttack(status,22,true),false);
assert.equal(cyber.canAttack({...status,cooldown:0},22,true),true);
assert.equal(cyber.canAttack({...status,cooldown:0},22,false),false);
assert.equal(cyber.canAttack({...status,cooldown:0},22),false);
assert.equal(cyber.canAttack({...status,emitter:false,cooldown:0},22,true),false);
assert.equal(cyber.canAttack({...status,cooldown:0},0,true),false);
assert.equal(cyber.canAttack({...status,cooldown:0},22,1),false);
const moduleState=cyber.parse('COLONY|1|5|CYBER_MODULE|17|0|1|1|0|0');
assert.equal(moduleState.hardware,1);
assert.equal(moduleState.requested,true);
assert.equal(moduleState.effective,false);
assert.equal(moduleState.isolated,false);
assert.equal(cyber.parse('COLONY|1|5|CYBER_MODULE|17|1|0|1|0|0').hardware,0);
assert.equal(cyber.parse('COLONY|1|5|CYBER_TARGETS_RESET|17').ship,17);
assert.equal(cyber.parse('COLONY|1|5|CYBER_TARGET|17|22|7000').distance,7000);
assert.equal(cyber.parse('COLONY|1|6|CYBER_RESULT|991|3|ATTACK_RESISTED').result,'ATTACK_RESISTED');
assert.deepEqual(cyber.parse('COLONY|1|6|CYBER_DEFENSE|17|4|88|2'),
 {tick:6,type:'CYBER_DEFENSE',ship:17,remaining:4,source:88,exposed:2});
assert.equal(cyber.parse('COLONY|1|6|CYBER_INCOMING|17|88|992|APPLIED').result,'APPLIED');
for(const forged of [
 'COLONY|1|x|CYBER_STATUS|17|1|0|0|1',
 'COLONY|1|5|CYBER_STATUS|17|2|0|0|1',
 'COLONY|1|5|CYBER_MODULE|17|2|1|1|0|0',
 'COLONY|1|5|CYBER_MODULE|17|0|0|1|1|0',
 'COLONY|1|5|CYBER_TARGET|17|17|5',
 'COLONY|1|5|CYBER_TARGET|17|22|9000',
 'COLONY|1|5|CYBER_RESULT|0|3|APPLIED',
 'COLONY|1|5|CYBER_RESULT|991|3|<script>',
 'COLONY|1|5|CYBER_STATUS|17|1|0|0|1|extra',
 'COLONY|1|6|CYBER_DEFENSE|17|0|88|2',
 'COLONY|1|6|CYBER_DEFENSE|17|4|0|2',
 'COLONY|1|6|CYBER_DEFENSE|17|10|88|2',
 'COLONY|1|6|CYBER_INCOMING|17|17|990|APPLIED',
 'COLONY|1|6|CYBER_INCOMING|17|88|0|APPLIED',
 'COLONY|1|6|CYBER_INCOMING|17|88|990|<script>'
 ])assert.throws(()=>cyber.parse(forged),undefined,forged);
console.log('PASS SHIP-STATUS-05: strictly parsed committed cyber cockpit telemetry, effective state, and target gates');
// Exercise the actual DOM wiring, not only the parser: disabled until a
// current-tick physically powered emitter and target observation both exist.
// No simulated browser command may be mistaken for a native combat result.
class FakeOption {
  constructor(text,value){this.text=text;this.value=value;}
}
class FakeNode {
  constructor(){
    this.children=[];this.options=[];this.listeners={};
    this.value='';this.textContent='';this.style={};this.disabled=false;
  }
  replaceChildren(...children){
    this.children=children;
    this.options=children.filter(c=>c instanceof FakeOption);
    if(this.options.length && !this.options.some(o=>o.value===this.value))
      this.value=this.options[0].value;
  }
  append(...children){this.children.push(...children);}
  addEventListener(event,fn){this.listeners[event]=fn;}
}
const nodes=new Map();
global.Option=FakeOption;
global.document={
  getElementById(id){
    if(!nodes.has(id))nodes.set(id,new FakeNode());
    return nodes.get(id);
  },
  createElement(){return new FakeNode();}
};
let emitted=[];
global.window={GalaxyColonyCommand:(action,args)=>{emitted.push([action,args]);}};
delete require.cache[require.resolve('../viewer/cyber-controls.js')];
const cockpit=require('../viewer/cyber-controls.js');
function telemetry(value){assert.equal(cockpit.accept(value),true);}
const attack=nodes.get('cyberAttack'),select=nodes.get('cyberTarget');
telemetry('COLONY|1|5|CYBER_STATUS|17|1|0|0|1');
telemetry('COLONY|1|5|CYBER_TARGET|17|22|700');
select.value='22';select.listeners.change();
telemetry('COLONY|1|5|CYBER_MODULE|17|0|3|1|0|0');
assert.equal(attack.disabled,true,'confused-off emitter is not ready');
telemetry('COLONY|1|5|CYBER_MODULE|17|0|3|1|1|0');
assert.equal(attack.disabled,false,'same-tick powered emitter enables request');
attack.listeners.click();
assert.deepEqual(emitted,[['colonyCyberAttack',{target:22}]]);
telemetry('COLONY|1|5|CYBER_MODULE|17|0|3|1|0|1');
assert.equal(attack.disabled,true,'isolated emitter cannot attack');
assert.match(nodes.get('cyberModuleList').children[0].children[0].textContent,/ISOLATED/);
assert.doesNotMatch(nodes.get('cyberModuleList').children[0].children[0].textContent,/CONFUSED/);
telemetry('COLONY|1|6|CYBER_STATUS|17|1|0|0|1');
assert.equal(attack.disabled,true,'previous-tick hardware and target are stale');
telemetry('COLONY|1|6|CYBER_MODULE|17|0|3|1|1|0');
assert.equal(attack.disabled,true,'previous-tick target must not authorize request');
telemetry('COLONY|1|6|CYBER_TARGET|17|22|700');
select.value='22';select.listeners.change();
assert.equal(attack.disabled,false,'fresh telemetry restores ready state');
assert.equal(emitted.length,1,'no forged combat result is generated');
telemetry('COLONY|1|6|CYBER_DEFENSE|17|4|88|2');
assert.match(nodes.get('cyberDefense').textContent,/CONFUSION ACTIVE/);
assert.match(nodes.get('cyberDefense').textContent,/4 tick/);
telemetry('COLONY|1|6|CYBER_INCOMING|17|88|992|APPLIED');
assert.match(nodes.get('cyberAlert').textContent,/disrupted your modules/);
telemetry('COLONY|1|6|CYBER_INCOMING|17|88|993|ATTACK_RESISTED');
assert.match(nodes.get('cyberAlert').textContent,/ATTACK RESISTED/);
telemetry('COLONY|1|7|CYBER_STATUS|17|1|0|0|1');
assert.match(nodes.get('cyberDefense').textContent,/No active Confusion/);
telemetry('COLONY|1|7|CYBER_DEFENSE|17|0|0|0');
assert.match(nodes.get('cyberDefense').textContent,/No active Confusion/);
const html=require('node:fs').readFileSync(
 require('node:path').join(__dirname,'../viewer/interactive.html'),'utf8');
for(const id of ['cyberDefense','cyberAlert','cyberModuleList','cyberAttack'])
 assert.match(html,new RegExp('id="'+id+'"'));
delete global.document;delete global.window;delete global.Option;
console.log('PASS SHIP-STATUS-05: committed-only DOM controls, isolation feedback, stale telemetry rejection');

