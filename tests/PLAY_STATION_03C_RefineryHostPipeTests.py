#!/usr/bin/env python3
"""Executes real original SQLite-world station refinery protocol through its child host."""
from pathlib import Path
import subprocess,sys,tempfile,time
fixture,host=map(Path,sys.argv[1:3]);assert fixture.is_file() and host.is_file()
with tempfile.TemporaryDirectory() as directory:
    db=str(Path(directory)/'world.sqlite')
    generated=subprocess.run([str(fixture),db,'--03c-fixture'],capture_output=True,text=True,timeout=45)
    assert generated.returncode==0,(generated.stdout,generated.stderr)
    line=next((x for x in generated.stdout.splitlines() if x.startswith('FIXTURE|')),None)
    assert line,line
    _,station,lot,ship,mass=line.split('|')
    station,lot,ship,mass=map(int,(station,lot,ship,mass))
    p=subprocess.Popen([str(host),db,'--explicit-test-station'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1)
    def raw(cmd):
        time.sleep(.057)
        p.stdin.write(cmd+'\n');p.stdin.flush()
        line=p.stdout.readline().strip()
        assert line,cmd
        return line.split('|')
    first=p.stdout.readline().strip().split('|')
    assert first[:2]==['PERSON','2'] and first[6]=='4' and int(first[7])==station,first
    session=int(first[2]);generation=int(first[5]);req=1
    denied=raw(f'REFVIEW|{session}|{req}|{generation}|{station}')
    assert denied[:4]==['DENY','2','1','refinery-terminal-distance-or-custody'],denied
    # Movement is original persisted station position, not a UI teleport.
    for req in range(2,10):
        step=raw(f'WALK|{session}|{req}|{generation}|100|0')
        assert step[:2]==['PERSON','2'],step
        generation=int(step[5]);assert int(step[9])<=2400
    assert int(step[9])==2400,step
    def snapshot(cmd):
        header=raw(cmd)
        assert header[:2]==['REFHEAD','3'],header
        lot_count=int(header[15]);min_count=int(header[17]);rows=[]
        for _ in range(lot_count+min_count):rows.append(p.stdout.readline().strip().split('|'))
        end=p.stdout.readline().strip().split('|')
        assert end[:2]==['REFEND','3'] and int(end[6])==lot_count and int(end[7])==min_count,end
        assert all(row[2:4]==header[2:4] for row in rows)
        return header,rows,end
    # First live read from original PersistentWorld; no fake UI material.
    header,rows,end=snapshot(f'REFVIEW|{session}|10|{generation}|{station}')
    assert header[7]=='1' and int(header[13])==1 and int(header[14])==0,header
    assert int(header[15])>=1 and any(x[0]=='REFLOT' and int(x[4])==lot and int(x[7])==1 for x in rows),rows
    assert end[8]=='0' and end[9]=='0',end
    generation=int(end[5]);req=11
    head,rows,ref=snapshot(f'REFINE|{session}|{req}|{generation}|{lot}')
    assert ref[8]=='1' and ref[9]=='1000',ref
    assert int(head[13])==0 and int(head[14])==1000,head
    minerals={x[4]:int(x[5]) for x in rows if x[0]=='REFMIN'}
    assert minerals=={'nickel_iron':860,'troilite':80,'olivine':30,'pyroxene':15,'other':15},minerals
    generation=int(ref[5]);
    denied=raw(f'REFINE|{session}|11|{generation}|{lot}')
    assert denied[:4]==['DENY','2','11','replay'],denied
    denied=raw(f'REFINE|{session}|12|{generation}|{lot}')
    assert denied[:4]==['DENY','2','12','no-owned-refinery-or-lot'],denied
    p.stdin.write('QUIT\n');p.stdin.flush();assert p.wait(timeout=10)==0,p.stderr.read()
    verify=subprocess.run([str(fixture),db,'--03c-verify'],capture_output=True,text=True,timeout=40)
    assert verify.returncode==0,(verify.stdout,verify.stderr)
    # Original persistent save survives process restart with the exact output.
    p=subprocess.Popen([str(host),db,'--explicit-test-station'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1)
    first=p.stdout.readline().strip().split('|')
    assert first[:2]==['PERSON','2'] and first[6]=='4' and int(first[9])==2400,first
    head,rows,end=snapshot(f'REFVIEW|{first[2]}|1|{first[5]}|{station}')
    assert int(head[14])==1000 and int(head[13])==0 and int(end[9])==0,(head,end)
    p.stdin.write('QUIT\n');p.stdin.flush();assert p.wait(timeout=10)==0,p.stderr.read()
print('PLAY-STATION-03C PASS: real person range, refused remote terminal, physical walk, sourced ore lot, original SQLite refinery command, 1000kg outputs, replay/no-lot denial, restart')
