#!/usr/bin/env python3
"""B6.4 live regional navigation: real C++ host commands, committed SGVIEW frames,
real directed gates, same SQLite cargo, scoped survey. No mock teleportation.
"""
import argparse
import collections
from pathlib import Path
import subprocess
import tempfile
import threading
import sqlite3


def test(exe):
    with tempfile.TemporaryDirectory(prefix='galaxy-b64-') as td:
        db=Path(td)/'regional.sqlite'
        proc=subprocess.Popen([str(exe),str(db),'20','440'],stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE,stderr=subprocess.PIPE,bufsize=0)
        errors=[]
        def read_err():
            for raw in proc.stderr:
                errors.append(raw.decode('ascii','replace').strip())
        thread=threading.Thread(target=read_err,daemon=True);thread.start()
        entities={}; ship=None;system=1;enter_tick=0
        snapshots=[];commands=[]; travelled=set();went=False
        mining_requested=False;undock_requested=False;assist_requested=set()
        probes=False; scanned=False;completed=False
        def send(s):
            proc.stdin.write((s+'\n').encode('ascii'));proc.stdin.flush()
            commands.append((tick,s))
        try:
            while True:
                line=proc.stdout.readline()
                if not line:break
                head=line.decode('ascii').strip()
                if not head.startswith('SGVIEW|'):raise AssertionError('missing canonical frame')
                h=head.split('|'); tick=int(h[5]);current=int(h[6])
                if h[2]=='1':
                    entities.clear();snapshots.append(current)
                if current!=system:
                    assert h[2]=='1', f'new system {current} without clean snapshot'
                    system=current;enter_tick=tick
                while True:
                    row=proc.stdout.readline()
                    if not row:raise AssertionError('truncated SGVIEW')
                    t=row.decode('ascii').strip()
                    if t=='END':break
                    a=t.split('|')
                    if a[0]=='E':
                        entities[int(a[1])]=a
                        assert int(a[2])==current, f'cross-system entity leaked: {a}'
                    elif a[0]=='R':entities.pop(int(a[1]),None)
                for a in entities.values():
                    assert int(a[2])==current, 'stale off-system entity retained'
                ships=[a for a in entities.values() if a[3]=='0' and a[4]=='7']
                if ships:ship=ships[0]
                # Follow the original 3-hop shortcut, with each jump individually authorized.
                expected_next={1:2,2:6,6:7}
                if tick==0:send('ROUTE|7|S')
                elif tick==1 and not undock_requested:
                    send('UNDOCK');undock_requested=True
                elif current==1 and tick==3 and not mining_requested:
                    send('MINE|4');mining_requested=True
                elif current in expected_next and ship:
                    cargo=int(ship[10]);dest=expected_next[current]
                    leg=[e for e in errors if f'|LEG|{dest}|' in e and int(e.split('|')[2])>=enter_tick]
                    gateid=int(leg[-1].rsplit('|',1)[-1]) if leg else None
                    ready=(current!=1 or cargo==20) and tick-enter_tick>=8
                    if ready and gateid is not None:
                        gate=entities.get(gateid)
                        if gate:
                            dx=int(ship[5])-int(gate[5]);dy=int(ship[6])-int(gate[6]);
                            if dx*dx+dy*dy<=2200*2200:
                                if current not in travelled:
                                    send('JUMP_HOP');travelled.add(current)
                            elif current not in assist_requested:
                                send('TRAVEL_HOP');assist_requested.add(current)
                elif current==7 and ship:
                    if not probes and tick-enter_tick>=8:
                        send('PROBE_DEPLOY');probes=True
                    elif probes and not scanned and tick-enter_tick>=10:
                        send('PROBE_SCAN');scanned=True
                    elif scanned and not completed and tick-enter_tick>=13:
                        send('ROUTE|1|Q');completed=True
            proc.stdin.close()
            rc=proc.wait(timeout=8);thread.join(timeout=2)
            assert rc==0, '\n'.join(errors[-25:])
        finally:
            if proc.poll() is None:proc.kill();proc.wait(timeout=3)
            proc.stdout.close();proc.stderr.close()
        assert snapshots[:4]==[1,2,6,7],f'wrong regional journey {snapshots} commands={commands} errors={errors[-40:]}'
        assert len(snapshots)==4,f'unexpected reentry {snapshots}'
        assert len([v for k,v in commands if v=='JUMP_HOP'])==3,commands
        assert any('|ROUTE|7|S|' in e and '|1-2-6-7' in e for e in errors),errors
        assert any('|ROUTE|1|Q|' in e and '|7-5-4-3-1' in e for e in errors),errors[-50:]
        assert any('|P|703|' in e for e in errors),errors[-60:]
        assert ship is not None and int(ship[10])==20,ship
        with sqlite3.connect(db) as conn:
            # Exact durable transfer chain, with cargo conservation on every leg.
            jumps=conn.execute('SELECT source_system,dest_system,cargo_at_departure,completed_tick '
                               'FROM jump_ledger ORDER BY accepted_tick').fetchall()
            assert [(x[0],x[1],x[2]) for x in jumps]==[(1,2,20),(2,6,20),(6,7,20)], jumps
            assert all(x[3] is not None for x in jumps),jumps
            assert conn.execute('SELECT COUNT(*) FROM observations WHERE system=7 AND player=7').fetchone()[0]>=1
            assert conn.execute('SELECT COUNT(*) FROM observations WHERE system!=7').fetchone()[0]==0, \
                'scanner created observations in off-system partitions'
        print('PASS B6.4 live 1→2→6→7, 3 physical gate transfers, cargo preserved, 4 local snapshots, scanner on arrival, safer return advisory')

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--executable',required=True,type=Path)
    a=ap.parse_args();test(a.executable)
