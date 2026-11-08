"""Test-only harness. Native game uses no Python/browser/HTTP runtime."""
import os, sys, sqlite3, subprocess, tempfile, time
from pathlib import Path

exe=Path(sys.argv[1]);assert exe.is_file(),exe

def connected(path):
    p=subprocess.Popen([str(exe),str(path)],stdin=subprocess.PIPE,
                       stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1)
    initial=p.stdout.readline().strip().split('|')
    assert len(initial)==11 and initial[:2]==['POSE','1'], initial
    return p,int(initial[2]),int(initial[4]),tuple(map(int,initial[5:8]))

def query(p,line):
    p.stdin.write(line+'\n');p.stdin.flush()
    response=p.stdout.readline().strip()
    assert response,response
    return response.split('|')

with tempfile.TemporaryDirectory() as tmp:
    save=Path(tmp)/'authoritative.sqlite'
    p,token,seq,coords=connected(save)
    assert (seq,coords)==(0,(0,0,0)),(seq,coords)
    with sqlite3.connect(save) as db:
        baseline=db.execute('select count(*) from operation_log').fetchone()[0]
    assert query(p,f'MOVE|{token+1}|1|0|80|0')==['DENY','1','1','session']
    assert query(p,f'MOVE|{token}|1|0|99|99')==['DENY','1','1','speed']
    assert query(p,f'MOVE|{token}|1|0|80|0')==['DENY','1','1','replayed-request']
    assert query(p,f'MOVE|{token}|2|0|0|0')==['DENY','1','2','zero-step']
    time.sleep(.065)
    ack=query(p,f'MOVE|{token}|3|0|80|0')
    assert ack[:4]==['ACK','1',str(token),'3'] and tuple(map(int,ack[4:8]))==(1,80,0,0),ack
    seq=1
    time.sleep(.065)
    assert query(p,f'MOVE|{token}|4|0|20|0')==['DENY','1','4','stale-pose']
    time.sleep(.065)
    ack=query(p,f'MOVE|{token}|5|1|60|80')
    assert ack[:4]==['ACK','1',str(token),'5'] and tuple(map(int,ack[4:8]))==(2,140,0,80),ack
    p.stdin.write('QUIT\n');p.stdin.flush()
    assert p.wait(timeout=4)==0,p.stderr.read()
    assert not p.stderr.read()
    with sqlite3.connect(save) as db:
        assert db.execute('SELECT COUNT(*) FROM operation_log').fetchone()[0]==baseline+2
        rows=db.execute('SELECT seq,world_gen,world_tick FROM sg_fp3d02c_step ORDER BY seq').fetchall()
        assert len(rows)==2 and all(db.execute('SELECT kind FROM operation_log WHERE generation=?',(g,)).fetchone()[0]==2 for n,g,t in rows)
        row=db.execute('SELECT seq,x_mm,z_mm FROM sg_fp3d02c_pose').fetchone()
        assert row==(2,140,80),row
    # Exact same native world from SQLite, no new simulated starting position.
    p2,tok2,seq2,xyz2=connected(save)
    assert seq2==2 and xyz2==(140,0,80), (seq2,xyz2)
    assert tok2!=token,'new process session ID must change'
    time.sleep(.065)
    ack=query(p2,f'MOVE|{tok2}|1|2|-40|0')
    assert ack[:4]==['ACK','1',str(tok2),'1'] and tuple(map(int,ack[4:8]))==(3,100,0,80),ack
    p2.stdin.close()
    assert p2.wait(timeout=4)==0,p2.stderr.read()
    with sqlite3.connect(save) as db:
        assert db.execute('SELECT COUNT(*) FROM operation_log').fetchone()[0]==baseline+3
        assert db.execute('SELECT x_mm,z_mm FROM sg_fp3d02c_pose').fetchone()==(100,80)
print('FP3D02F PASS: native child pipe command admission, OWNER binding, one atomic world/pose STEP, invalid denial, EOF cleanup, SQLite restart')
