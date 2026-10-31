const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const root=path.resolve(__dirname,'..');
const {decode,names}=require('../viewer/colony.js');
test('colony status, geology, minerals and outcomes validated',()=>{
 assert.equal(decode('COLONY|1|1|STATUS|24|8|0|0|0|16829897').raw,8);
 assert.equal(decode('COLONY|1|0|ROCK|4|0|70|1700|2442484|2442484').classId,0);
 assert.equal(decode('COLONY|1|0|MINERAL|4|serpentine|550').share,550);
 assert.equal(decode('COLONY|1|1|RESULT|900000000001|APPLIED|8').transferred,8);
 assert.deepEqual(names,['Carbonaceous','Silicate','Metal-rich']);
});
test('reject malformed or forged geology and overflow',()=>{
 for(const bad of [
  'COLONY|1|1|STATUS|24|8|0|0|0|16829897|7',
  'COLONY|1|1|STATUS|0|8|0|0|0|16829897',
  'COLONY|1|1|ROCK|4|9|70|1700|2442484|2442484',
  'COLONY|1|1|ROCK|4|0|70|1700|100|101',
  'COLONY|1|1|MINERAL|4|plutonium|550',
  'COLONY|1|1|MINERAL|4|serpentine|1100',
  'COLONY|1|1|RESULT|1|OK<script>|8',
  'COLONY|1|1|STATUS|24|8|0|0|0|9999999999999999',
  'COLONY|1|1|BYPASS|ADMIN',
 ])assert.throws(()=>decode(bad),Error,bad);
});
test('visual colony controls and server-only request boundary exist',()=>{
 const html=fs.readFileSync(path.join(root,'viewer/interactive.html'),'utf8');
 const relay=fs.readFileSync(path.join(root,'live/interactive_relay.py'),'utf8');
 const js=fs.readFileSync(path.join(root,'viewer/interactive.js'),'utf8');
 assert.match(html,/id="colonyPanel" hidden/);
 for(const id of ['colonyScan','colonyMine','colonyRefine','colonyFabricate','colonyBuild','colonyGeology','colonyCrew'])
  assert.match(html,new RegExp('id="'+id+'"'));
 assert.match(relay,/if self.server.colony_mode:/);
 assert.match(js,/source.addEventListener\('colony'/);
 assert.match(html,/#colonyPanel\[hidden\]/);
});
