"""Actual C++ host + SQLite save restart; optional local relay HTTP trust boundary."""
import argparse
import base64
import http.client
import json
import re
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
from pathlib import Path

def direct(host,save,commands):
    p=subprocess.run([str(host),str(save),'20','14'],input=commands.encode('ascii'),capture_output=True,timeout=8)
    assert p.returncode==0,p.stderr.decode(errors='replace')[-1200:]
    frame=p.stdout.decode('ascii')
    records=p.stderr.decode('ascii')
    assert frame.count('SGVIEW|1|')==15,frame[:200]
    return frame,records

def request(port,method,path,body=None,origin=True):
    conn=http.client.HTTPConnection('127.0.0.1',port,timeout=8)
    payload=json.dumps(body).encode() if body is not None else None
    headers={'Content-Type':'application/json'} if body is not None else {}
    if body is not None and origin: headers['Origin']='http://127.0.0.1:'+str(port)
    conn.request(method,path,body=payload,headers=headers)
    resp=conn.getresponse();data=resp.read();status=resp.status;conn.close()
    return status, data

def test_relay(host,relay):
    with tempfile.TemporaryDirectory(prefix='b70b_relay_') as tmp:
        with socket.socket() as s:
            s.bind(('127.0.0.1',0));port=s.getsockname()[1]
        proc=subprocess.Popen([sys.executable,str(relay),'--host-exe',str(host),'--colony',
             '--save',str(Path(tmp)/'colony.sqlite'),'--port',str(port),'--tick-ms','20','--max-ticks','100'],
             stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            for _ in range(60):
                try:
                    status,data=request(port,'GET','/session')
                    if status==200:break
                except (OSError,TimeoutError):pass
                time.sleep(.025)
            else:raise AssertionError('relay failed to listen')
            session=json.loads(data)
            assert session['colony_mode'] is True and session['qa_mode'] is False,session
            token=session['token'];base={'token':token}
            status,_=request(port,'POST','/command',{**base,'action':'qaGate','from':2,'to':6,'online':False})
            assert status==403,status
            status,_=request(port,'POST','/command',{**base,'action':'jump'})
            assert status==403,status
            status,_=request(port,'POST','/command',{'token':'bad','action':'colonyBuild'})
            assert status==403,status
            status,_=request(port,'POST','/command',{**base,'action':'colonyBuild'},origin=False)
            assert status==403,status
            status,_=request(port,'POST','/command',{**base,'action':'colonyMine','amount':True})
            assert status==400,status
            for action,extra in [('colonyScan',{}),('colonyMine',{'amount':8}),('colonyRefine',{'amount':8}),('colonyFabricate',{'amount':4}),('colonyBuild',{})]:
                status,data=request(port,'POST','/command',{**base,'action':action,**extra})
                assert status==202,(action,status,data)
            status,html=request(port,'GET','/viewer/interactive.html')
            assert status==200 and b'colonyPanel' in html
            status,js=request(port,'GET','/viewer/colony.js')
            assert status==200 and b'GalaxyColonyContract' in js
            # Wait for the complete host run; frames and telemetry are buffered.
            proc_host_finished=False
            for _ in range(120):
                with sqlite3.connect(str(Path(tmp)/'colony.sqlite')) as db:
                    count=db.execute('SELECT MAX(generation) FROM snapshots').fetchone()[0] or 0
                if count>25:proc_host_finished=True;break
                time.sleep(.025)
            assert proc_host_finished,'host did not persist streamed ticks'
            with sqlite3.connect(str(Path(tmp)/'colony.sqlite')) as db:
                blob=db.execute('SELECT state FROM snapshots ORDER BY generation DESC LIMIT 1').fetchone()[0].decode('ascii')
                assert blob.startswith('galaxy-kernel-v10|'),blob[:40]
                assert len(re.findall(r'^lifesupport\|',blob,re.M))==1
                assert len(re.findall(r'^geology\|',blob,re.M))==3
                assert 'outpost|' in blob,'relay actions failed to construct outpost'
            print('PASS B7.0b relay: local token/origin/schema/QA rejection, authorized scan-mining-refining-fabrication-building')
        finally:
            proc.terminate()
            try:proc.communicate(timeout=4)
            except subprocess.TimeoutExpired:proc.kill();proc.communicate()

def check(host,relay):
    with tempfile.TemporaryDirectory(prefix='b70b_live_') as td:
        save=Path(td)/'colony.sqlite'
        frames,records=direct(host,save,'SCAN\nMINE|8\nREFINE|8\nFABRICATE|4\nBUILD\n')
        assert '|NEW_COLONY' in records
        assert '|ROCK|4|0|' in records and '|ROCK|5|1|' in records and '|ROCK|6|2|' in records
        assert '|MINERAL|4|serpentine|550' in records
        assert records.count('|RESULT|')==4,records[-500:]
        assert records.count('|APPLIED|8')>=3,records[-500:]
        assert '|STATUS|24|0|0|0|1|' in records
        assert '|2|7|' in frames,'owned outpost not streamed'
        with sqlite3.connect(save) as db:
            snapshot=db.execute('SELECT state FROM snapshots ORDER BY generation DESC LIMIT 1').fetchone()[0].decode('ascii')
            assert snapshot.startswith('galaxy-kernel-v10|'),snapshot[:60]
            assert len(re.findall(r'^lifesupport\|',snapshot,re.M))==1
            assert len(re.findall(r'^geology\|',snapshot,re.M))==3
        frames2,records2=direct(host,save,'SCAN\n')
        assert '|RESUMED' in records2
        assert '|STATUS|24|0|0|0|1|' in records2
        assert '|ROCK|4|0|' in records2
        print('PASS B7.0b live colony: committed SGVIEW, true construction and mass, mineral telemetry, restart')
    test_relay(host,relay)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--host',required=True,type=Path);p.add_argument('--relay',required=True,type=Path)
    a=p.parse_args();check(a.host,a.relay)
