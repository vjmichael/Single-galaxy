#!/usr/bin/env python3
"""B6.6 read-only infrastructure telemetry from an actual host, not mocked events."""
import argparse
import re
import sqlite3
import subprocess
import tempfile
from pathlib import Path


def check(executable):
    with tempfile.TemporaryDirectory(prefix='sg-b66-live-') as td:
        db=Path(td)/'world.sqlite'
        result=subprocess.run([str(executable),str(db),'20','45'],input=b'',
                              stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=14)
        assert result.returncode==0, result.stderr.decode(errors='replace')[-1500:]
        stream=result.stdout.decode('ascii')
        messages=result.stderr.decode('ascii').splitlines()
        assert stream.count('SGVIEW|1|')==46, stream[:400]
        status=[]
        for line in messages:
            if '|GATE_STATUS|' in line:
                parts=line.split('|')
                assert len(parts)==11, parts
                _,ver,tick,event,fromsys,tosys,gate,condition,workers,spares,online=parts
                values=list(map(int,[tick,fromsys,tosys,gate,condition,workers,spares,online]))
                assert int(ver)==1 and event=='GATE_STATUS' and int(fromsys)==1,parts
                assert 0<=int(condition)<=1000 and int(workers)==2 and int(online)==1,parts
                status.append(values)
        assert len(status)>=10, status
        first={v[3]:v for v in status if v[0]==0}
        last={v[3]:v for v in status if v[0]==40}
        assert first and set(first)==set(last),(first,last)
        for gate in first:
            assert first[gate][5]==last[gate][5]==2
            assert last[gate][6]==first[gate][6]-4,(gate,first,last)
        with sqlite3.connect(db) as sql:
            state=sql.execute('SELECT state FROM snapshots ORDER BY generation DESC LIMIT 1').fetchone()[0].decode('ascii')
            assert state.startswith('galaxy-kernel-v6|'),state[:50]
            gateops=[r for r in state.splitlines() if r.startswith('gateops|')]
            assert len(gateops)==9,len(gateops)
            assert all(int(row.split('|')[5])==2 for row in gateops)
        print('PASS B6.6 live current-system gate condition telemetry, staff and parts consumption, all nine links durable')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--executable',required=True,type=Path)
    args=p.parse_args();check(args.executable)
