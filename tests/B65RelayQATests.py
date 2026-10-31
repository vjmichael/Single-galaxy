#!/usr/bin/env python3
"""Test-only relay: no QA actions without explicit --qa-test switch."""
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


def session(exe,relay,enabled):
    with socket.socket() as s:
        s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    base=f'http://127.0.0.1:{port}'
    args=[sys.executable,str(relay),'--host-exe',str(exe),'--port',str(port),
          '--tick-ms','35','--max-ticks','95']
    if enabled:args.append('--qa-test')
    proc=subprocess.Popen(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    try:
        for _ in range(100):
            try:
                if b'alive' in request(base,'/healthz'):break
            except (OSError,urllib.error.URLError):time.sleep(.035)
        else:raise AssertionError('relay unavailable')
        context=json.loads(request(base,'/session'))
        check(context['qa_mode'] is enabled,'QA mode status not honest to client')
        auth={'token':context['token']}
        html=request(base,'/viewer/interactive.html')
        check(b'id="qaPanel" hidden' in html,'QA controls not hidden by default')
        gate={**auth,'action':'qaGate','from':2,'to':6,'online':False}
        if not enabled:
            for c in [gate,{**auth,'action':'qaRoute','destination':7,'policy':'S'},
                      {**auth,'action':'qaStop'}]:
                code,_=post(base,c)
                check(code==403,'QA control accessible without explicit test mode')
            code,_=post(base,{**auth,'action':'planRoute','destination':7,'policy':'S'})
            check(code==202,'ordinary navigation broken')
        else:
            for bogus in [
                {**auth,'action':'qaRoute','destination':True,'policy':'S'},
                {**auth,'action':'qaGate','from':2,'to':6,'online':1},
                {**auth,'action':'qaGate','from':2,'to':2,'online':False},
                {**auth,'action':'qaGate','from':2,'to':6,'online':False,'issuer':999},
            ]:
                code,_=post(base,bogus);check(code==400,'unsafe test command accepted')
            code,_=post(base,gate,origin=False)
            check(code==403,'cross-origin QA request accepted')
            code,_=post(base,{**gate,'token':'wrong'})
            check(code==403,'invalid QA token accepted')
            code,_=post(base,{**auth,'action':'qaRoute','destination':7,'policy':'S'})
            check(code==202,'QA route denied in QA mode')
            time.sleep(.16)
            code,_=post(base,gate);check(code==202,'QA outage denied in QA mode')
            time.sleep(.18)
            code,_=post(base,{**auth,'action':'qaStop'});check(code==202,'QA stop denied')
        events=event_types(request(base,'/events').decode('ascii'))
        if enabled:
            check(any('|QA_MODE|1' in e for e in events),'QA mode not visibly disclosed')
            check(any('|QA_GATE|2|6|0' in e for e in events),'committed QA outage not reported')
            check(any('|REROUTE|7|S|' in e for e in events),'route not invalidated by outage')
            check(any('|QA_STOPPED|1' in e for e in events),'QA stop not reported')
        else:check(not any('|QA_MODE|' in e for e in events),'QA leaked into ordinary mode')
    finally:
        proc.terminate()
        try:proc.communicate(timeout=3)
        except subprocess.TimeoutExpired:proc.kill();proc.communicate()

def run(exe,relay):
    session(exe,relay,False)
    session(exe,relay,True)
    print('PASS B6.5 QA relay opt-in, token/origin/schema checks, default denial, committed outage and reroute')
if __name__=='__main__':
    a=argparse.ArgumentParser();a.add_argument('--executable',type=pathlib.Path,required=True)
    a.add_argument('--relay',type=pathlib.Path,required=True)
    args=a.parse_args();run(args.executable,args.relay)
