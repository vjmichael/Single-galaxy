#!/usr/bin/env python3
"""Actual two-human security-only relay integration.

A native C++ fixture creates two *existing* ships in one canonical SQLite game
for this test only. Normal game's single-vessel genesis is never extended.
The native host remains the single world writer; both browser sessions use
distinct authenticated account cookies and exactly owner-addressed telemetry.
"""
import argparse
import base64
import http.client
import json
import os
from pathlib import Path
import re
import sqlite3
import subprocess
import sys
import tempfile
import time

from SHIPSTATUS06_LiveCyberHostTests import (
    auth_row,free_port,request,wait_for
)

def read_private(stream,deadline=5):
    observed=[]
    end=time.monotonic()+deadline
    while time.monotonic()<end:
        line=stream.fp.readline()
        if not line:break
        if line.startswith(b'event: frame') or line.startswith(b'event: survey'):
            raise AssertionError('Secondary human received global game frame')
        if not line.startswith(b'data: '):continue
        try:decoded=base64.b64decode(line[6:].strip(),validate=True)
        except ValueError:continue
        if decoded.startswith(b'SGVIEW|') or decoded.startswith(b'PRIVATE|'):
            raise AssertionError('Global view or raw owner envelope leaked')
        if decoded.startswith(b'COLONY|1|'):
            row=decoded.decode('ascii')
            if '|SECURITY_' not in row:raise AssertionError('Foreign diagnostics leaked')
            observed.append(row)
            if '|SECURITY_CALL_INBOX|' in row or '|SECURITY_ERROR|NO_OWNED_COLONY' in row:
                return observed
    return observed

def run(fixture,host,relay,bridge):
    with tempfile.TemporaryDirectory(prefix='sg-status07g-live-') as temp:
        root=Path(temp);db=root/'multi-world.sqlite';accounts=root/'accounts.json'
        seeded=subprocess.run([str(fixture),'--seed',str(db)],
            capture_output=True,text=True,timeout=15)
        assert seeded.returncode==0,(seeded.stdout,seeded.stderr)
        match=re.search(r'ships ([0-9]+) ([0-9]+)',seeded.stdout)
        assert match,seeded.stdout
        alice_ship,bob_ship=map(int,match.groups())
        accounts.write_text(json.dumps({'version':1,'accounts':[
            auth_row('alice',7,'alice-private-hail-password'),
            auth_row('bob',8,'bob-private-hail-password')]}),'utf8')
        if os.name!='nt':os.chmod(accounts,0o600)
        port=free_port()
        command=[
            sys.executable,str(relay),'--host-exe',str(host),
            '--colony','--save',str(db),'--port',str(port),
            '--tick-ms','20','--max-ticks','1200',
            '--accounts',str(accounts),'--shipping-bridge',str(bridge)]
        proc=subprocess.Popen(command,stdout=subprocess.DEVNULL,
                              stderr=subprocess.PIPE)
        con=None
        try:
            def ready():
                if proc.poll() is not None:
                    raise RuntimeError('original game host terminated')
                try:return request(port,'GET','/healthz')[0]==200
                except OSError:return False
            wait_for(ready,'original two-human host unavailable')
            code,alice,cookie1=request(port,'POST','/api/login',{
                'username':'alice','password':'alice-private-hail-password'})
            assert code==200 and alice['playerId']==7
            cookie1=cookie1.split(';',1)[0]
            code,bob,cookie2=request(port,'POST','/api/login',{
                'username':'bob','password':'bob-private-hail-password'})
            assert code==200 and bob['playerId']==8
            cookie2=cookie2.split(';',1)[0]
            code,session2,_=request(port,'GET','/session',cookie=cookie2)
            assert code==200 and session2['security_only'] is True
            assert session2['commands']==[
                'colonySecurityRefresh','colonySecurityCall',
                'colonySecurityCallReply','colonySecurityNotifyAck']
            assert request(port,'POST','/command',{
                'token':bob['csrf'],'action':'colonyCyberAttack',
                'target':alice_ship},cookie2)[0]==403
            assert request(port,'POST','/command',{
                'token':bob['csrf'],'action':'colonyFireShip',
                'target':alice_ship},cookie2)[0]==403
            assert request(port,'POST','/command',{
                'token':bob['csrf'],'action':'colonySecurityCallReply',
                'request':'999999','choice':'identify',
                'owner':7},cookie2)[0]==400
            assert request(port,'GET','/viewer/security-only.html')[0]==200

            status,result,_=request(port,'POST','/command',{
                'token':alice['csrf'],'action':'colonySecurityCall',
                'ship':bob_ship},cookie1)
            assert status==202 and result['status']=='queued_not_committed'
            def received_call():
                try:
                    with sqlite3.connect(db,timeout=.5) as conn:
                        return conn.execute(
                            'SELECT COUNT(*) FROM sg_ship_hail07e WHERE kind=1 AND '
                            'caller_owner=7 AND recipient_ship=?',
                            (bob_ship,)).fetchone()[0]==1
                except sqlite3.Error:return False
            wait_for(received_call,'real player-7 to player-8 call not committed')
            with sqlite3.connect(db) as conn:
                raw=conn.execute(
                    'SELECT event_id FROM sg_ship_hail07e '
                    'WHERE kind=1 AND caller_owner=7 AND recipient_ship=?',
                    (bob_ship,)).fetchone()[0]
            assert raw>0
            # The second owner never receives frame / survey events.
            # New connection runs a native private REFRESH from committed
            # SQLite; reconnect does not rely on an earlier volatile SSE.
            con=http.client.HTTPConnection('127.0.0.1',port,timeout=4)
            con.request('GET','/events',headers={'Cookie':cookie2})
            stream=con.getresponse()
            assert stream.status==200
            inbox=read_private(stream)
            assert any('|SECURITY_CALL_INBOX|'+str(raw)+'|'+
                str(alice_ship)+'|7' in row for row in inbox),inbox
            assert not any('|SECURITY_CASE|' in row for row in inbox)
            assert request(port,'POST','/command',{
                'token':bob['csrf'],'action':'colonySecurityCallReply',
                'request':str(raw),'choice':'identify'},cookie1)[0]==403
            code,result,_=request(port,'POST','/command',{
                'token':bob['csrf'],'action':'colonySecurityCallReply',
                'request':str(raw),'choice':'identify'},cookie2)
            assert code==202 and result['status']=='queued_not_committed'
            def answered():
                try:
                    with sqlite3.connect(db,timeout=.5) as conn:
                        return conn.execute(
                            'SELECT COUNT(*) FROM sg_ship_hail07e WHERE kind=2 '
                            'AND recipient_controller=8 AND related=?',
                            (raw,)).fetchone()[0]==1
                except sqlite3.Error:return False
            wait_for(answered,'human recipient identification not committed')
            status,result,_=request(port,'POST','/command',{
                'token':alice['csrf'],'action':'colonySecurityCall',
                'ship':bob_ship},cookie1)
            assert status==202
            def penalized():
                try:
                    with sqlite3.connect(db,timeout=.5) as conn:
                        return conn.execute(
                            'SELECT COUNT(*) FROM sg_ship_hail07e WHERE kind=4 '
                            'AND caller_owner=7 AND recipient_ship=?',
                            (bob_ship,)).fetchone()[0]==1
                except sqlite3.Error:return False
            wait_for(penalized,'post-identification nuisance not committed')
            assert request(port,'POST','/command',{
                'token':bob['csrf'],'action':'colonySecurityClaim',
                'radius':3000},cookie2)[0]==403
        finally:
            if con:con.close()
            proc.terminate()
            try:proc.communicate(timeout=5)
            except subprocess.TimeoutExpired:proc.kill();proc.communicate()
        print('PASS STATUS-07G: two actual login accounts, native shared world, '
              'owner-private SSE, no global frames, identify, penalty and wrong-owner denial')

if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--fixture',required=True,type=Path)
    p.add_argument('--host',required=True,type=Path)
    p.add_argument('--relay',required=True,type=Path)
    p.add_argument('--bridge',required=True,type=Path)
    args=p.parse_args()
    run(*(getattr(args,x).resolve() for x in ('fixture','host','relay','bridge')))
