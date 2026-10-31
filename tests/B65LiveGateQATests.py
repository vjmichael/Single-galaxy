#!/usr/bin/env python3
"""B6.5: local QA auto-input uses ordinary physical commands and durable gate admin.
QA is explicit; output is not a covert anti-cheat notification.
"""
import argparse
import os
import sqlite3
import subprocess
import tempfile
import threading
from pathlib import Path


def test(exe):
    with tempfile.TemporaryDirectory(prefix='galaxy-b65-') as td:
        db=Path(td)/'world.sqlite'
        env=os.environ.copy();env['SINGLE_GALAXY_QA_MODE']='1'
        proc=subprocess.Popen([str(exe),str(db),'20','550'],stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env,bufsize=0)
        records=[]
        def drain_stderr():
            for row in proc.stderr:
                records.append(row.decode('ascii','replace').rstrip())
        err_thread=threading.Thread(target=drain_stderr,daemon=True);err_thread.start()
        snapshots=[];system=1;ship_id=None;last_cargo=None;gate_down=False;gate_up=False
        route_revised=False;route_restored=False;done=False
        def send(line):
            proc.stdin.write((line+'\n').encode('ascii'));proc.stdin.flush()
        try:
            while True:
                line=proc.stdout.readline()
                if not line:break
                header=line.decode('ascii','replace').strip().split('|')
                assert header[0]=='SGVIEW' and header[1]=='1',header
                tick=int(header[5]);current=int(header[6]);is_snapshot=header[2]=='1'
                if is_snapshot:snapshots.append(current)
                if current!=system:
                    assert is_snapshot,(system,current,tick)
                    system=current
                while True:
                    line=proc.stdout.readline()
                    if not line:raise AssertionError('Truncated frame')
                    s=line.decode('ascii','replace').strip()
                    if s=='END':break
                    e=s.split('|')
                    if e[0]=='E' and e[3]=='0' and e[4]=='7':
                        assert int(e[2])==current
                        if ship_id is None:ship_id=int(e[1])
                        assert ship_id==int(e[1])
                        cargo=int(e[10]);
                        if last_cargo is None:last_cargo=cargo
                        assert cargo==last_cargo, 'QA travel changed cargo'
                if tick==0:send('QA_ROUTE|7|S')
                if tick==16:send('QA_GATE|2|6|0');gate_down=True
                if system==7 and not gate_up and tick>45:
                    send('QA_GATE|2|6|1');gate_up=True
                    send('ROUTE|1|S')
                if any('|REROUTE|7|S|' in e for e in records):route_revised=True
                if any('|QA_DONE|7' in e for e in records):done=True
                if any('|ROUTE|1|S|' in e and '|7-6-2-1' in e for e in records):route_restored=True
            proc.stdin.close()
            rc=proc.wait(timeout=6);err_thread.join(timeout=2)
            assert rc==0,(rc,records[-25:])
        finally:
            if proc.poll() is None:proc.kill();proc.wait(timeout=2)
            proc.stdout.close();proc.stderr.close()
        assert gate_down and gate_up,(gate_down,gate_up,snapshots)
        assert done and route_revised and route_restored,(done,route_revised,route_restored,records[-40:])
        assert snapshots[0]==1 and snapshots[-1]==7 and len(snapshots)>=4,snapshots
        assert not any(a==2 and b==6 for a,b in zip(snapshots,snapshots[1:])),snapshots
        assert any('|QA_GATE|2|6|0' in e for e in records),records[-30:]
        assert any('|QA_GATE|2|6|1' in e for e in records),records[-30:]
        assert any('QA AUDIT' in e for e in records), 'QA activity must be auditable'
        with sqlite3.connect(db) as conn:
            jumps=conn.execute('SELECT source_system,dest_system,cargo_at_departure,completed_tick FROM jump_ledger ORDER BY accepted_tick').fetchall()
            assert len(jumps)==len(snapshots)-1,(jumps,snapshots)
            assert all(row[3] is not None for row in jumps),jumps
            assert all(row[2]==last_cargo for row in jumps),jumps
        print('PASS B6.5 QA auto physical route; live gate disruption/reroute/repair; durable jumps; no cross-system teleport')

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--executable',type=Path,required=True)
    a=ap.parse_args();test(a.executable)
