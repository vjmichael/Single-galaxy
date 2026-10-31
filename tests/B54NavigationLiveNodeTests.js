'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const bytes=fs.readFileSync(path.join(__dirname,'../recordings/b54_off_axis_station_trade.sgview'),'utf8');
const frames=bytes.split(/(?=SGVIEW\|1\|)/).filter(x=>x.startsWith('SGVIEW|'));
test('B5.4 committed off-axis ship motion is non-teleporting in both axes',()=>{
  assert.equal(frames.length,126);
  let movingY=false,nonzeroY=false,inSpaceB=false,dockedB=false,credits=0;
  let cargoAtB=false,marketB=false;
  for (const frame of frames){
    let dock=0;
    for (const line of frame.split('\n')){
      const t=line.split('|');
      if(t[0]==='E'&&t[1]==='2'){
        const y=Number(t[6]),vy=Number(t[8]),x=Number(t[5]);
        if(vy!==0)movingY=true;
        if(y!==0)nonzeroY=true;
        if(x>=10000 && y>=6750 && Number(t[10])===20)inSpaceB=true;
      }
      if(t[0]==='Q'&&t[1]==='2'&&t[2]==='1'&&t[3]==='4')dockedB=true;
      if(t[0]==='H'&&t[1]==='4'&&t[2]==='7'&&t[3]==='20')cargoAtB=true;
      if(t[0]==='W'&&t[1]==='7')credits=Number(t[2]);
      if(t[0]==='M'&&t[1]==='4'&&t[2]==='20'&&t[3]==='900')marketB=true;
    }
  }
  assert.ok(movingY && nonzeroY && inSpaceB && dockedB && cargoAtB && marketB);
  assert.equal(credits,100);
});
