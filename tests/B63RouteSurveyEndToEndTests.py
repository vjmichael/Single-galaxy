#!/usr/bin/env python3
"""B6.3 live route and survey-history acceptance against C++ host + SQLite.
Commands are sent on committed frame ticks. No simulated responses in Python.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
import re
import sqlite3


def run(exe):
    with tempfile.TemporaryDirectory(prefix='galaxy-b63-') as td:
        db=Path(td)/'world.sqlite'
        script={0:'ROUTE|2|S',1:'UNDOCK',5:'PROBE_DEPLOY',6:'PROBE_SCAN',
                7:'SURVEY_HISTORY',8:'PROBE_RECOVER',10:'JUMP',
                15:'ROUTE|1|Q',16:'SURVEY_HISTORY',20:'PROBE_DEPLOY',
                21:'PROBE_SCAN',22:'PROBE_RECOVER',23:'SURVEY_HISTORY',26:'JUMP',
                36:'SURVEY_HISTORY',37:'ROUTE|2|Q'}
        proc=subprocess.Popen([str(exe),str(db),'50','55'],stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE,stderr=subprocess.PIPE,bufsize=0)
        buf=[];header=None
        try:
            while True:
                line=proc.stdout.readline()
                if not line:break
                buf.append(line)
                if line.startswith(b'SGVIEW|'):header=line.decode('ascii').split('|')
                if line==b'END\n':
                    tick=int(header[5]);header=None
                    if tick in script:
                        proc.stdin.write((script[tick]+'\n').encode('ascii'))
                        proc.stdin.flush()
            proc.stdin.close()
            error=proc.stderr.read().decode('utf-8','replace')
            assert proc.wait(timeout=6)==0,error[-2000:]
        finally:
            if proc.poll() is None:proc.kill();proc.wait(timeout=3)
            proc.stdout.close();proc.stderr.close()
        stream=b''.join(buf).decode('ascii')
        snapshots=re.findall(r'^SGVIEW\|1\|1\|\d+\|\d+\|\d+\|([12])\|',stream,re.M)
        assert snapshots==['1','2','1'],f'bad system transitions: {snapshots}'
        assert '|ROUTE|2|S|100|1-2\n' in error,error
        assert '|ROUTE|1|Q|160|2-1\n' in error,error
        assert '|ROUTE|2|Q|380|1-2\n' in error,error
        assert '|HISTORY|103|' in error,'origin history not emitted'
        assert '|HISTORY|203|' in error,'frontier history not emitted: '+error
        assert error.count('|ARCHIVE|103|')>=1,'history not restored after system reentry'
        with sqlite3.connect(db) as conn:
            latest=conn.execute('SELECT system,site,tick FROM observations ORDER BY system,site').fetchall()
            hist=conn.execute('SELECT system,site,tick FROM observation_history ORDER BY revision').fetchall()
            assert any(x[0]==1 for x in latest) and any(x[0]==2 for x in latest),(latest,error)
            assert len(hist)>=len(latest)>=2,(hist,latest)
            assert len({x[0] for x in latest})==2,'systems mixed'
            assert conn.execute('SELECT COUNT(*) FROM observation_history WHERE player != 7').fetchone()[0]==0
        print('PASS B6.3 live: shortest/safer route advisories, two jumps, archival persistence, re-entry restore')

if __name__=='__main__':
    a=argparse.ArgumentParser();a.add_argument('--executable',required=True,type=Path);args=a.parse_args()
    run(args.executable)
