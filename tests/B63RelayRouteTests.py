#!/usr/bin/env python3
"""B6.3 browser-to-relay-to-C++ route and history integration (no synthetic events)."""
import argparse
import json
import pathlib
import socket
import subprocess
import sys
import time
import urllib.error
from B52InteractiveEndToEndTests import request,post,check
from B61LiveScanningTests import event_types


def run(exe,relay):
    with socket.socket() as s:
        s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    base=f'http://127.0.0.1:{port}'
    proc=subprocess.Popen([sys.executable,str(relay),'--host-exe',str(exe),'--port',str(port),
                           '--tick-ms','45','--max-ticks','44'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    try:
        for _ in range(100):
            try:
                if b'alive' in request(base,'/healthz'):break
            except (OSError,urllib.error.URLError):time.sleep(.04)
        else:raise AssertionError('relay unavailable')
        session=json.loads(request(base,'/session'));token=session['token']
        check({'planRoute','surveyHistory'}.issubset(session['commands']),'new controls not offered')
        html=request(base,'/viewer/interactive.html')
        check(b'id="planRoute"' in html and b'id="surveyHistory"' in html,'browser controls missing')
        good={'token':token}
        for invalid in [
            {**good,'action':'planRoute','destination':0,'policy':'S'},
            {**good,'action':'planRoute','destination':8,'policy':'Q'},
            {**good,'action':'planRoute','destination':2,'policy':'X'},
            {**good,'action':'planRoute','destination':True,'policy':'S'},
            {**good,'action':'planRoute','destination':2,'policy':'S','owner':7},
            {**good,'action':'surveyHistory','system':2},
        ]:
            code,_=post(base,invalid);check(code==400,'untrusted route/history input accepted: '+str(invalid))
        code,_=post(base,{**good,'action':'planRoute','destination':2,'policy':'S'},origin=False)
        check(code==403,'cross-origin planner permitted')
        code,_=post(base,{'token':'wrong','action':'surveyHistory'})
        check(code==403,'invalid session token permitted')
        for cmd in [
            {**good,'action':'planRoute','destination':2,'policy':'S'},
            {**good,'action':'planRoute','destination':2,'policy':'Q'},
            {**good,'action':'surveyHistory'},
        ]:
            code,_=post(base,cmd);check(code==202,'valid command rejected')
            time.sleep(.11)
        blob=request(base,'/events').decode('ascii')
        ev=event_types(blob)
        check(any('|ROUTE|2|S|100|1-2' in x for x in ev),'shortest route not relayed')
        check(any('|ROUTE|2|Q|380|1-2' in x for x in ev),'safer route not relayed')
        check(any('|HISTORY_DONE|0' in x for x in ev),'history completion lost')
        print('PASS B6.3 relay: planner and history authorization, schema rejection, real C++ sensor events')
    finally:
        proc.terminate()
        try:proc.communicate(timeout=3)
        except subprocess.TimeoutExpired:proc.kill();proc.communicate()

if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--executable',required=True,type=pathlib.Path)
    p.add_argument('--relay',required=True,type=pathlib.Path)
    a=p.parse_args();run(a.executable,a.relay)
