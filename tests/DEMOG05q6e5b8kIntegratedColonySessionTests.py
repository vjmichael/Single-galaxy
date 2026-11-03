#!/usr/bin/env python3
"""Live original colony world + v63-compatible user sessions + same-save shipment reader.

The colony process is the only game writer. This test refuses cross-account
control, native unprefixed commands, and second shipping-writer ownership.
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


def port():
    with socket.socket() as s:
        s.bind(('127.0.0.1',0))
        return s.getsockname()[1]


def account(name,ident,password):
    salt=os.urandom(32)
    secret=hashlib.scrypt(password.encode(),salt=salt,n=16384,r=8,p=1,dklen=64)
    return {'username':name,'playerId':ident,'salt':salt.hex(),'hash':secret.hex()}


def request(port,method,path,data=None,cookie=None,csrf=None,origin=True):
    headers={'Host':f'127.0.0.1:{port}'}
    if data is not None:
        headers['Content-Type']='application/json'
        if origin:headers['Origin']=f'http://127.0.0.1:{port}'
    if cookie:headers['Cookie']=cookie
    if csrf:headers['X-SG-CSRF']=csrf
    body=json.dumps(data).encode() if data is not None else None
    con=http.client.HTTPConnection('127.0.0.1',port,timeout=8)
    try:
        con.request(method,path,body=body,headers=headers)
        response=con.getresponse();status=response.status
        payload=response.read();cookie_header=response.getheader('Set-Cookie')
        try:reply=json.loads(payload)
        except (ValueError,UnicodeDecodeError):reply=payload
        return status,reply,cookie_header
    finally:con.close()


def run(host,relay,bridge):
    with tempfile.TemporaryDirectory(prefix='sg-v64-native-auth-') as native:
        checked=subprocess.run([str(host),str(Path(native)/'native.sqlite'),'20','8','--authenticated-transport'],
            input='SCAN\nAUTH|8|SCAN\nAUTH|7|SCAN\n',text=True,capture_output=True,timeout=8)
        assert checked.returncode==0,checked.stderr[-300:]
        assert checked.stderr.count('ERROR|UNAUTHORIZED_PRINCIPAL')==2,checked.stderr[-200:]
        assert checked.stderr.count('SCAN_DONE|')==2,'unauthorized scans ran or authorized scan was rejected'
    with tempfile.TemporaryDirectory(prefix='sg-v64-secure-') as d:
        world=Path(d)/'colony.sqlite';accounts=Path(d)/'accounts.json';p=port()
        rows=[account('colony-owner',7,'private-owner-password'),
              account('freight-carrier',8,'private-carrier-password'),
              account('outsider-player',11,'private-stranger-password')]
        accounts.write_text(json.dumps({'version':1,'accounts':rows}),encoding='utf-8')
        os.chmod(accounts,0o600)
        cmd=[sys.executable,str(relay),'--host-exe',str(host),'--colony',
             '--save',str(world),'--port',str(p),'--tick-ms','20','--max-ticks','1000',
             '--accounts',str(accounts),'--shipping-bridge',str(bridge)]
        proc=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        try:
            for _ in range(160):
                if proc.poll() is not None:raise RuntimeError('relay exited: '+proc.stderr.read().decode(errors='replace'))
                try:
                    if request(p,'GET','/healthz')[0]==200 and world.is_file():break
                except OSError:pass
                time.sleep(.05)
            else:raise AssertionError('relay never became ready')
            assert request(p,'GET','/api/session')[0]==401
            assert request(p,'GET','/session')[0]==403
            assert request(p,'GET','/api/shipments')[0]==401
            assert request(p,'GET','/events')[0]==403
            assert request(p,'POST','/command',{'token':'x','action':'colonyScan'})[0]==403
            assert request(p,'POST','/api/login',{'username':'colony-owner','password':'private-owner-password'},origin=False)[0]==403
            assert request(p,'POST','/api/login',{'username':'colony-owner','password':'incorrect'})[0]==401
            assert request(p,'POST','/api/login',{'username':'colony-owner','password':'private-owner-password','playerId':8})[0]==400

            def login(username,password):
                status,data,cookie=request(p,'POST','/api/login',{'username':username,'password':password})
                assert status==200,(status,data)
                assert data['playerId']==(7 if username=='colony-owner' else 8 if username=='freight-carrier' else 11)
                assert 'HttpOnly' in cookie and 'SameSite=Strict' in cookie
                return cookie.split(';')[0],data['csrf']
            foreign,foreign_csrf=login('freight-carrier','private-carrier-password')
            assert request(p,'GET','/session',cookie=foreign)[0]==403
            assert request(p,'GET','/events',cookie=foreign)[0]==403
            assert request(p,'GET','/api/shipments',cookie=foreign)[1]=={'ok':True,'shipments':[]}
            assert request(p,'POST','/command',{'token':foreign_csrf,'action':'colonyBuild'},cookie=foreign)[0]==403
            assert request(p,'POST','/api/logout',{},cookie=foreign,csrf=foreign_csrf)[0]==200
            assert request(p,'GET','/api/session',cookie=foreign)[0]==401

            owner,csrf=login('colony-owner','private-owner-password')
            session_status,session,_=request(p,'GET','/session',cookie=owner)
            assert session_status==200 and session['token']==csrf and session['colony_mode'] is True
            assert request(p,'GET','/api/shipments',cookie=owner)[1]=={'ok':True,'shipments':[]}
            assert request(p,'POST','/command',{'token':csrf,'action':'qaGate','from':2,'to':6,'online':False},cookie=owner)[0]==403
            assert request(p,'POST','/command',{'token':foreign_csrf,'action':'colonyBuild'},cookie=owner)[0]==403
            assert request(p,'POST','/command',{'token':csrf,'action':'colonyBuild'},cookie=owner,origin=False)[0]==403
            status,html,_=request(p,'GET','/viewer/interactive.html')
            assert status==200 and b'colonySessionPanel' in html and b'colony-shipping.js' in html
            assert request(p,'GET','/viewer/colony-shipping.js')[0]==200
            for action,data in [('colonyScan',{}),('colonyMine',{'amount':8}),('colonyRefine',{'amount':8}),
                                ('colonyFabricate',{'amount':4}),('colonyBuild',{})]:
                status,answer,_=request(p,'POST','/command',{'token':csrf,'action':action,**data},cookie=owner)
                assert status==202,(action,status,answer)

            # Two native processes must not act as separate freight/world writers.
            second=subprocess.run([str(bridge),str(world),'--writer','5153','world-return-net','home','east,west'],
                                  capture_output=True,text=True,timeout=5)
            assert second.returncode!=0 and ('another' in second.stderr.lower() or 'already' in second.stderr.lower()),second.stderr
            for _ in range(150):
                with sqlite3.connect(world) as db:
                    gen=db.execute('SELECT MAX(generation) FROM snapshots').fetchone()[0] or 0
                if gen>=20:break
                time.sleep(.04)
            else:raise AssertionError('native simulation failed to advance')
            with sqlite3.connect(world) as db:
                state=db.execute('SELECT state FROM snapshots ORDER BY generation DESC LIMIT 1').fetchone()[0].decode()
                assert 'outpost|' in state,'signed-in colony owner was unable to construct the outpost'
            assert request(p,'POST','/api/logout',{},cookie=owner,csrf=csrf)[0]==200
            assert request(p,'GET','/api/session',cookie=owner)[0]==401
            assert request(p,'POST','/command',{'token':csrf,'action':'colonyBuild'},cookie=owner)[0]==403
            print('PASS v64: v63 credentials; owner-only native command; same-world shipment status; writer lease; no anonymous SSE; restart-safe SQLite world')
        finally:
            proc.terminate()
            try:proc.communicate(timeout=4)
            except subprocess.TimeoutExpired:proc.kill();proc.communicate()

def completed_freight_world(host,relay,bridge,producer):
    """Read two real physically-settled consignments from one active colony host."""
    with tempfile.TemporaryDirectory(prefix='sg-v64-freight-') as d:
        world=Path(d)/'freight.sqlite';accounts=Path(d)/'accounts.json';p=port()
        subprocess.run([str(producer)],env={**os.environ,'SG_SHIPPING_STATUS_FIXTURE':str(world),
            'SG_SHIPPING_RUN_ID':'v64_settled'},capture_output=True,check=True,timeout=30)
        rows=[account('source-owner',7,'private-source-password'),account('carrier-owner',8,'private-carrier-password'),
              account('original-receiver',9,'private-receiver-password'),account('alternate-owner',10,'private-alternate-password'),
              account('no-shipment-owner',11,'private-outsider-password')]
        accounts.write_text(json.dumps({'version':1,'accounts':rows}),encoding='utf-8');os.chmod(accounts,0o600)
        cmd=[sys.executable,str(relay),'--host-exe',str(host),'--colony','--save',str(world),'--port',str(p),
             '--tick-ms','25','--max-ticks','600','--accounts',str(accounts),'--shipping-bridge',str(bridge)]
        proc=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        try:
            for _ in range(150):
                if proc.poll() is not None:raise RuntimeError('host exited: '+proc.stderr.read().decode(errors='replace'))
                try:
                    if request(p,'GET','/healthz')[0]==200:break
                except OSError:pass
                time.sleep(.04)
            else:raise AssertionError('freight host not ready')
            def shipments(name,password):
                code,identity,cookie=request(p,'POST','/api/login',{'username':name,'password':password})
                assert code==200,(code,identity)
                code,data,_=request(p,'GET','/api/shipments',cookie=cookie.split(';')[0])
                assert code==200,(code,data)
                return data['shipments']
            source=shipments('source-owner','private-source-password');assert len(source)==2,source
            assert all(x['role']=='source' and x['settled'] for x in source),source
            carrier=shipments('carrier-owner','private-carrier-password');assert len(carrier)==2
            original=shipments('original-receiver','private-receiver-password');assert len(original)==2
            alternate=shipments('alternate-owner','private-alternate-password')
            assert len(alternate)==1 and alternate[0]['phase']=='redirected_settled',alternate
            assert shipments('no-shipment-owner','private-outsider-password')==[]
            assert request(p,'POST','/api/shipment/dispatch',{'id':'frames-third-party'})[0]==404,                'independent shipping writer must not be exposed in active colony host'
            print('PASS v64: original live colony host opened the same 4-owner settled freight world; principal-scoped statuses, no second writer')
        finally:
            proc.terminate()
            try:proc.communicate(timeout=5)
            except subprocess.TimeoutExpired:proc.kill();proc.communicate()


if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--host',required=True,type=Path);ap.add_argument('--relay',required=True,type=Path);ap.add_argument('--bridge',required=True,type=Path);ap.add_argument('--producer',required=True,type=Path)
    args=ap.parse_args();run(args.host,args.relay,args.bridge);completed_freight_world(args.host,args.relay,args.bridge,args.producer)
