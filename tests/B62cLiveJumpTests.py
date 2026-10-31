#!/usr/bin/env python3
"""B6.2c live C++ host integration: two real systems, actual commands and sensor scopes.
No fake world-state production in Python. Browser rendering is NOT tested here.
"""
import argparse
import pathlib
import subprocess
import tempfile
import time
import re
import sqlite3
import shutil

def require(cond,msg):
    if not cond:raise AssertionError(msg)

def run(executable,root,recording_out=None):
    with tempfile.TemporaryDirectory(prefix='b62c-live-') as tmp:
        t=pathlib.Path(tmp);db=t/'world.sqlite';log=t/'frames.sgview';errors=t/'errors.log'
        # Drive commands off committed *simulation ticks* instead of wall-clock
        # sleeps. This remains deterministic when other tests load the CPU.
        commands={0:'UNDOCK',3:'MINE|4',11:'JUMP',
                  15:'DSCAN|0|360',16:'PROBE_DEPLOY',17:'PROBE_SCAN',
                  18:'SURVEY_ARCHIVE',19:'PROBE_RECOVER',
                  21:'DOCK',25:'UNLOAD|20',27:'SELL',30:'UNDOCK',34:'JUMP'}
        with open(log,'wb') as outf,open(errors,'wb') as errf:
            p=subprocess.Popen([str(executable),str(db),'50','115'],
                               stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=errf,bufsize=0)
            current_header=None
            try:
                while True:
                    line=p.stdout.readline()
                    if not line:break
                    outf.write(line)
                    if line.startswith(b'SGVIEW|'):
                        current_header=line.decode('ascii').split('|')
                    elif line==b'END\n':
                        require(current_header is not None,'END without frame header')
                        tick=int(current_header[5]);current_header=None
                        cmd=commands.get(tick)
                        if cmd:
                            require(p.poll() is None,'host died before '+cmd)
                            p.stdin.write((cmd+'\n').encode('ascii'));p.stdin.flush()
                p.stdin.close()
                require(p.wait(timeout=5)==0,'host exited nonzero')
            finally:
                if p.poll() is None:p.kill();p.wait(timeout=3)
                p.stdout.close()
        raw=log.read_text();err=errors.read_text()
        headers=re.findall(r'^SGVIEW\|1\|([12])\|([0-9]+)\|([0-9]+)\|([0-9]+)\|([12])\|',raw,re.M)
        require(len(headers)==116,'incorrect live frame count')
        switches=[(int(h[0]),int(h[4]),int(h[2])) for h in headers if h[0]=='1']
        require(len(switches)==3,'expected initial, frontier, and home snapshots: '+str(switches))
        require([x[1] for x in switches]==[1,2,1],'wrong jump route: '+str(switches))
        require(all(int(h[2])==n for n,h in enumerate(headers)),'non-monotonic wire sequence')
        require('|SYSTEM|2\n' in err and '|KNOWN|201|' in err and '|KNOWN|202|' in err,
                'frontier survey entry missing')
        require('|P|203|' in err and '|P|204|' in err,'frontier hidden sites not discovered')
        require('|P|103|' not in err and '|P|104|' not in err,'origin system survey leaked into frontier scan')
        require('|RECOVER|1\n' in err,'frontier probe not recovered')
        require('INTERACTIVE PASS' in err,'host did not complete')
        # Validate EACH frame's authoritative hash, sequence and current system
        # using exactly the same client parser as the web UI, not a second parser.
        node_script=r'''
const fs=require('fs');const assert=require('assert');
const c=require(process.argv[1]+'/viewer/contract.js');
const input=fs.readFileSync(process.argv[2],'utf8');
const frames=c.decodeFrames(input);let state=null,switches=[];
let maxOre=0,finalWallet=0;
for(const f of frames){
  const prev=state;state=c.applyNext(state,f);
  if(f.kind===1)switches.push(f.system);
  for(const e of state.entities.values())assert.equal(e.system,state.system);
  if(state.system===1)assert(![6,7,8].some(id=>state.entities.has(id)), 'frontier entity leaked into origin');
  if(state.system===2)assert(!state.entities.has(1), 'origin station leaked into frontier');
  assert.equal(state.wallets.size,1,'foreign wallet leaked');
  maxOre=Math.max(maxOre,state.markets.get(6)?.ore||0);
  finalWallet=state.wallets.get(7)||0;
}
assert.deepStrictEqual(switches,[1,2,1]);
assert.equal(finalWallet,140,'frontier trade credits failed');
assert.equal(maxOre,20,'ore did not arrive in frontier market');
console.log('PASS B6.2c Node parser: 116 authenticated-state frames, scopes, 2 jumps, 20 ore sale, 140 credits');
'''
        # Market IDs from deterministic setup: station 1, ship 2, rock 3,
        # stationB 4, gateA 5, frontier station 6, return gate 7, frontier rock 8.
        parsed=subprocess.run(['node','-e',node_script,str(root),str(log)],capture_output=True,text=True)
        require(parsed.returncode==0,'browser parser rejected live stream: '+parsed.stdout+parsed.stderr)
        if recording_out:
            pathlib.Path(recording_out).parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(log,recording_out)
        print('PASS B6.2c live C++: 2 authorized gate jumps, correct survey scope, probe recovery, station trade')
        print(parsed.stdout.strip())

if __name__=='__main__':
    a=argparse.ArgumentParser();a.add_argument('--executable',type=pathlib.Path,required=True)
    a.add_argument('--root',type=pathlib.Path,required=True)
    a.add_argument('--recording-out',type=pathlib.Path);args=a.parse_args()
    run(args.executable,args.root,args.recording_out)
