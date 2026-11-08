#!/usr/bin/env python3
"""Linux build acceptance for read-only native subprocess and restart.
Python is test runner ONLY, not game runtime, command relay, or installed dep.
"""
from __future__ import annotations
import subprocess, pathlib, tempfile, sys
BIN = pathlib.Path(sys.argv[1]).resolve()

def run(save: pathlib.Path, ticks: str):
    return subprocess.run([str(BIN), str(save), ticks], stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=20, text=True)

def parse_stream(text):
    rows=text.splitlines(); frames=[];p=0
    while p<len(rows):
        head=rows[p].split('|');p+=1
        assert len(head)==13 and head[:2]==['SGDESK','1'],head
        count=int(head[-1]);assert 1<=count<=64,count
        contacts=rows[p:p+count];p+=count
        assert rows[p]=='END';p+=1
        assert len(contacts)==count and all(len(x.split('|'))==7 and x.startswith('CONTACT|') for x in contacts)
        ship=[c for c in contacts if int(c.split('|')[1])==int(head[6])]
        assert len(ship)==1 and ship[0].split('|')[2:4]==['0','7'],ship
        assert ship[0].split('|')[4:6]==head[8:10],ship
        frames.append(head)
    return frames

with tempfile.TemporaryDirectory(prefix='sgdesk-') as tmp:
    save=pathlib.Path(tmp)/'world.sqlite'
    a=run(save,'3');assert a.returncode==0,(a.returncode,a.stderr)
    aa=parse_stream(a.stdout);assert len(aa)==4
    assert [int(x[3]) for x in aa]==[0,1,2,3]
    assert [int(x[4]) for x in aa]==[0,1,2,3]
    assert save.stat().st_size>1024
    b=run(save,'2');assert b.returncode==0,(b.returncode,b.stderr)
    bb=parse_stream(b.stdout);assert len(bb)==3
    assert [int(x[4]) for x in bb]==[3,4,5],bb
    assert bb[0][2]!=aa[0][2], 'new process must use a new epoch'
    assert aa[0][6:10]==bb[0][6:10],'ship custody changed after restart'
    bad=run(save,'1001');assert bad.returncode!=0
    assert bad.stdout==''
    bad2=run(pathlib.Path(tmp)/'missing'/'world.sqlite','1');assert bad2.returncode!=0
    assert bad2.stdout==''
    # Do not accept stdin as a game input channel even if an attacker sends THRUST.
    spoof=subprocess.run([str(BIN),str(save),'1'],input='THRUST|1|0\n',
        stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=15)
    assert spoof.returncode==0,spoof.stderr
    one=parse_stream(spoof.stdout);assert one[0][10:12]==['0','0']
print('FP3D-02B native read-only bridge PASS: 4+3+2 packets, restart, no spoof command authority')
