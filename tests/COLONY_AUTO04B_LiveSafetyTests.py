#!/usr/bin/env python3
"""COLONY-AUTO-04B: authentic colony relay -> single native world writer.

Checks authoritatively rejected occupied-site traps, same-save evidence, session
origin/CSRF/role protection, and that the UI uses native safety telemetry.
"""
import argparse
import hashlib
import http.client
import json
import os
from pathlib import Path
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time


def request(port, method, path, doc=None, cookie=None, csrf=None, origin=True):
    headers={'Host':f'127.0.0.1:{port}'}
    if doc is not None:
        headers['Content-Type']='application/json'
        if origin:headers['Origin']=f'http://127.0.0.1:{port}'
    if cookie:headers['Cookie']=cookie
    if csrf:headers['X-SG-CSRF']=csrf
    c=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
    try:
        c.request(method,path,json.dumps(doc).encode() if doc is not None else None,headers)
        r=c.getresponse();payload=r.read();status=r.status;set_cookie=r.getheader('Set-Cookie')
        try:value=json.loads(payload)
        except (ValueError,UnicodeDecodeError):value=payload
        return status,value,set_cookie
    finally:c.close()


def account(user,pid,pwd):
    salt=os.urandom(32)
    return {'username':user,'playerId':pid,'salt':salt.hex(),
        'hash':hashlib.scrypt(pwd.encode(),salt=salt,n=16384,r=8,p=1,dklen=64).hex()}


def port():
    with socket.socket() as s:
        s.bind(('127.0.0.1',0));return s.getsockname()[1]


def evidence(path):
    with sqlite3.connect(path) as db:
        try:return db.execute('SELECT event_count FROM sg_colony_safety_meta').fetchone()[0]
        except sqlite3.OperationalError:return 0


def run(host,relay,bridge):
    with tempfile.TemporaryDirectory(prefix='sg-auto04b-native-') as d:
        world=Path(d)/'native.sqlite'
        p=subprocess.run([str(host),str(world),'20','9','--authenticated-transport'],
          input='AUTH|8|SAFETY|1|SEAL_EXIT\nAUTH|7|SAFETY|1|SEAL_EXIT\nAUTH|7|SAFETY|1|DUMP_ATMOSPHERE\nAUTH|7|SAFETY|1|PRODUCTION_PREFLIGHT\nAUTH|7|SAFETY|1|ASSESS\n',
          text=True,capture_output=True,timeout=12)
        assert p.returncode==0,p.stderr[-1600:]
        assert 'ERROR|UNAUTHORIZED_PRINCIPAL' in p.stderr,p.stderr[-1600:]
        assert p.stderr.count('SAFETY_ORDER|1|REFUSED')==2,p.stderr[-1600:]
        assert p.stderr.count('SAFETY_ORDER|1|PREFLIGHT_ONLY')==1,p.stderr[-1600:]
        assert 'SAFETY_CHECK|1|0' in p.stderr
        # Telemetry is a proposed response, never a forged rescue completion.
        assert 'SAFETY_RESPONSE|1|0|0|0|0|0|0' in p.stderr
        assert evidence(world)==3
        with sqlite3.connect(world) as db:
            assert db.execute('SELECT COUNT(*) FROM sg_colony_safety_evidence WHERE attempted_hazard=1 AND allowed=0').fetchone()[0]==2
            assert db.execute('SELECT COUNT(*) FROM sg_colony_safety_evidence WHERE allowed=1').fetchone()[0]==1
            assert db.execute('SELECT COUNT(*) FROM sg_colony_safety_evidence WHERE actor_id!=7').fetchone()[0]==0
        p=subprocess.run([str(host),str(world),'20','5','--authenticated-transport'],
            input='AUTH|7|SAFETY|1|SEAL_EXIT\n',text=True,capture_output=True,timeout=10)
        assert p.returncode==0,p.stderr[-700:]
        assert evidence(world)==4,'evidence lost on restart'

    with tempfile.TemporaryDirectory(prefix='sg-auto04b-relay-') as d:
        world=Path(d)/'live.sqlite';accounts=Path(d)/'accounts.json';endpoint=port()
        accounts.write_text(json.dumps({'version':1,'accounts':[account('owner',7,'owner-test-pass'),
          account('other',8,'other-test-pass')]}),encoding='utf-8')
        os.chmod(accounts,0o600)
        proc=subprocess.Popen([sys.executable,str(relay),'--host-exe',str(host),'--colony',
           '--save',str(world),'--port',str(endpoint),'--tick-ms','30','--max-ticks','200',
           '--accounts',str(accounts),'--shipping-bridge',str(bridge)],
           stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        try:
            for _ in range(100):
                if proc.poll() is not None:raise RuntimeError(proc.stderr.read().decode(errors='replace'))
                try:
                    if request(endpoint,'GET','/healthz')[0]==200 and world.is_file():break
                except OSError:pass
                time.sleep(.05)
            else:raise RuntimeError('relay not ready')
            assert request(endpoint,'POST','/command',{'token':'bad','action':'colonySafetyPreflight','site':1})[0]==403
            code,record,cookie=request(endpoint,'POST','/api/login',{'username':'other','password':'other-test-pass'})
            assert code==200,(code,record)
            foreign=cookie.split(';')[0]
            assert request(endpoint,'POST','/command',{'token':record['csrf'],'action':'colonySafetyPreflight','site':1},foreign)[0]==403
            code,owner,cookie=request(endpoint,'POST','/api/login',{'username':'owner','password':'owner-test-pass'})
            assert code==200,(code,owner)
            session=cookie.split(';')[0]
            assert request(endpoint,'POST','/command',{'token':owner['csrf'],'action':'colonySafetyPreflight','site':1},session,origin=False)[0]==403
            assert request(endpoint,'POST','/command',{'token':'forged','action':'colonySafetyPreflight','site':1},session)[0]==403
            assert request(endpoint,'POST','/command',{'token':owner['csrf'],'action':'colonySafetyPreflight','site':True},session)[0]==400
            assert request(endpoint,'POST','/command',{'token':owner['csrf'],'action':'colonySafetyPreflight','site':1,'playerId':7},session)[0]==400
            # Ingress is a strict allowlist: a browser cannot synthesize forbidden
            # occupied-site orders or smuggle a native SAFETY subcommand.
            for action in ('colonySafetySealExit','colonySafetyDisableDistress',
                           'colonySafetyDumpAtmosphere','colonySafetyBlockRepair',
                           'SAFETY','nativeCommand'):
                code,_,_=request(endpoint,'POST','/command',
                    {'token':owner['csrf'],'action':action,'site':1},session)
                assert code in (400,403),(action,code)
            for malformed in (None,0,-1,1.0,'1',True,100000001):
                assert request(endpoint,'POST','/command',
                    {'token':owner['csrf'],'action':'colonySafetyAssess','site':malformed},
                    session)[0]==400
            assert evidence(world)==0,'rejected browser commands created a safety event'
            assert request(endpoint,'POST','/command',{'token':owner['csrf'],'action':'colonySafetyPreflight','site':1},session)[0]==202
            assert request(endpoint,'POST','/command',{'token':owner['csrf'],'action':'colonySafetyAssess','site':1},session)[0]==202
            for _ in range(80):
                if evidence(world)==1:break
                time.sleep(.04)
            else:raise AssertionError('authenticated safety order not committed by native world writer')
            with sqlite3.connect(world) as db:
                row=db.execute('SELECT actor_id,order_id,allowed FROM sg_colony_safety_evidence').fetchone()
                assert row==(7,1,1),row
                count=db.execute('SELECT COUNT(*) FROM sg_colony_safety_evidence').fetchone()[0]
                assert count==1
            status,page,_=request(endpoint,'GET','/viewer/interactive.html')
            assert status==200 and b'colonySafetyRisk' in page and b'colonySafetyPreflight' in page
            print('PASS AUTO04B authenticated relayed preflight, role/CSRF denials, actual native evidence, no second writer')
        finally:
            proc.terminate()
            try:proc.communicate(timeout=5)
            except subprocess.TimeoutExpired:proc.kill();proc.communicate()
    print('PASS AUTO04B private native command, occupied order refusal, restart persistence')

if __name__=='__main__':
    ap=argparse.ArgumentParser()
    for k in ('host','relay','bridge'):ap.add_argument('--'+k,required=True,type=Path)
    a=ap.parse_args();run(a.host,a.relay,a.bridge)
