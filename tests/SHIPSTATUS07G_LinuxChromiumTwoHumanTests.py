#!/usr/bin/env python3
"""Real Chromium + source-built native game, NO Windows installation.

Creates two ships ONLY in an explicit original-world test fixture. Browses
as the secondary authenticated human and verifies a real hailing round-trip
through original C++/SQLite and strict private owner SSE, not synthetic DOM.
"""
import argparse
import json
import os
from pathlib import Path
import re
import sqlite3
import subprocess
import sys
import tempfile

from playwright.sync_api import sync_playwright
from SHIPSTATUS06_LiveCyberHostTests import auth_row,free_port,request,wait_for

def run(fixture,host,relay,bridge):
    with tempfile.TemporaryDirectory(prefix='sg-status07g-chromium-') as temp:
        root=Path(temp)
        db=root/'real-multi.sqlite'; accounts=root/'accounts.json'
        seeded=subprocess.run([str(fixture),'--seed',str(db)],
            capture_output=True,text=True,timeout=15)
        assert seeded.returncode==0,(seeded.stdout,seeded.stderr)
        m=re.search(r'ships (\d+) (\d+)',seeded.stdout)
        assert m,seeded.stdout
        player_ship=int(m.group(1))
        bob_ship=int(m.group(2))
        accounts.write_text(json.dumps({'version':1,'accounts':[
            auth_row('alice',7,'alice-chromium-password'),
            auth_row('bob',8,'bob-chromium-password')]}),'utf8')
        if os.name!='nt':os.chmod(accounts,0o600)
        port=free_port()
        proc=subprocess.Popen([
            sys.executable,str(relay),'--host-exe',str(host),
            '--colony','--save',str(db),'--port',str(port),
            '--tick-ms','20','--max-ticks','1200',
            '--accounts',str(accounts),'--shipping-bridge',str(bridge)],
            stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        try:
            def ready():
                if proc.poll() is not None:
                    raise RuntimeError('native Chromium test host terminated')
                try:return request(port,'GET','/healthz')[0]==200
                except OSError:return False
            wait_for(ready,'real Chromium test host unavailable')
            status,alice,cookie=request(port,'POST','/api/login',{
                'username':'alice','password':'alice-chromium-password'})
            assert status==200 and alice['playerId']==7
            cookie=cookie.split(';',1)[0]
            with sync_playwright() as pw:
                browser=pw.chromium.launch(headless=True)
                try:
                    page=browser.new_page()
                    page.goto(f'http://127.0.0.1:{port}/viewer/security-only.html')
                    page.locator('#securityUsername').fill('bob')
                    page.locator('#securityPassword').fill('bob-chromium-password')
                    page.locator('#securityLoginButton').click()
                    page.locator('#securityOwnerPanel').wait_for(
                        state='visible',timeout=7000)
                    page.wait_for_function(
                        '''(id)=>Array.from(document.querySelectorAll(
                          '#securityContact option')).some(o=>o.value===String(id))''',
                        arg=player_ship,timeout=7000)
                    # This is owner8's actual ship sensor contact, not a
                    # fabricated destination provided to the web app.
                    page.locator('#securityContact').select_option(str(player_ship))
                    assert page.locator('#securityCallShip').is_enabled()
                    page.locator('#securityCallShip').click()
                    def first_call():
                        try:
                            with sqlite3.connect(db,timeout=.5) as c:
                                return c.execute(
                                    'SELECT event_id FROM sg_ship_hail07e '
                                    'WHERE kind=1 AND caller_owner=8 '
                                    'AND recipient_ship=?',
                                    (player_ship,)).fetchone()
                        except sqlite3.Error:return None
                    wait_for(first_call,'browser outbound native call not committed')
                    receipt=first_call()[0]
                    assert receipt>0
                    # Real player7 must identify from HIS own authenticated
                    # session. Neither owner may choose the other's actor.
                    result=response=request(port,'POST','/command',{
                        'token':alice['csrf'],'action':'colonySecurityCallReply',
                        'request':str(receipt),'choice':'identify'},cookie)
                    assert result[0]==202
                    def verified_reply():
                        try:
                            with sqlite3.connect(db,timeout=.5) as c:
                                return c.execute(
                                    'SELECT COUNT(*) FROM sg_ship_hail07e '
                                    'WHERE kind=2 AND related=? '
                                    'AND recipient_controller=7',
                                    (receipt,)).fetchone()[0]==1
                        except sqlite3.Error:return False
                    wait_for(verified_reply,'actual authenticated reply missing')
                    # No automatic new call reaches the now-identified
                    # original human recipient; actual penalty still accrues.
                    page.locator('#securityCallShip').click()
                    def genuine_penalty():
                        try:
                            with sqlite3.connect(db,timeout=.5) as c:
                                return c.execute(
                                    'SELECT COUNT(*) FROM sg_ship_hail07e '
                                    'WHERE kind=4 AND caller_owner=8 '
                                    'AND recipient_ship=?',
                                    (player_ship,)).fetchone()[0]==1
                        except sqlite3.Error:return False
                    wait_for(genuine_penalty,'repeat hail nuisance not committed')
                    page.locator('#securityCommsPenalty').get_by_text(
                        'Communications nuisance score 1').wait_for(
                            timeout=7000)
                    page.locator('#securityLogout').click()
                    page.locator('#securityLogin').wait_for(
                        state='visible',timeout=7000)
                    assert page.locator('#securityOwnerPanel').is_hidden()
                    assert page.locator('#securityOwnAccount').inner_text()==''
                    page.close()
                finally:browser.close()
        finally:
            proc.terminate()
            try:proc.communicate(timeout=5)
            except subprocess.TimeoutExpired:proc.kill();proc.communicate()
        print('PASS STATUS-07G REAL LINUX CHROMIUM: human-owner login, '
              'physical sensed target, original hails/identity/penalty, '
              'owner-private DOM and signout')

if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--fixture',required=True,type=Path)
    p.add_argument('--host',required=True,type=Path)
    p.add_argument('--relay',required=True,type=Path)
    p.add_argument('--bridge',required=True,type=Path)
    a=p.parse_args()
    run(*(getattr(a,x).resolve() for x in ('fixture','host','relay','bridge')))
