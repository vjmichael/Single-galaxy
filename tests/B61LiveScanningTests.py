#!/usr/bin/env python3
"""B6.1: real host scanning, server-supplied positions, private survey archive, relay auth.
All frame and survey observations originate from the C++ host. No browser claim.
"""
import argparse
import base64
import json
import pathlib
import socket
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
from B52InteractiveEndToEndTests import request, post, collect, extract_sse, check


def native(exe):
    with tempfile.TemporaryDirectory(prefix='b61-native-') as tmp:
        db=pathlib.Path(tmp)/'world.sqlite'
        p=subprocess.Popen([str(exe),str(db),'40','85'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        def send(action,delay=.16):
            p.stdin.write((action+'\n').encode('ascii'));p.stdin.flush();time.sleep(delay)
        try:
            send('DSCAN|0|360',.08) # rejected by authoritative dock state
            send('UNDOCK',.22)
            send('DSCAN|0|360',.18)
            send('PROBE_SCAN',.15) # no probes => zero hidden sites
            send('PROBE_DEPLOY',.19)
            send('PROBE_SCAN',.18)
            with sqlite3.connect(db) as con:
                obs=con.execute('select site,kind,confidence,published from observations order by site').fetchall()
                check(len(obs)==2,'scan did not persist both hidden sites')
                check(all(r[-1]==0 for r in obs),'private observations prematurely published')
            send('SURVEY_ARCHIVE',.16)
            send('SURVEY_PUBLISH',.16)
            send('SURVEY_ARCHIVE',.15)
            send('PROBE_RECOVER',.12)
            send('PROBE_SCAN',.12)
            stdout,stderr=p.communicate(timeout=8)
            check(p.returncode==0,'host crash: '+stderr.decode(errors='replace'))
            events=[x for x in stderr.decode('ascii').splitlines() if x.startswith('SURVEY|')]
            check(any('|ERROR|NOT_IN_SPACE' in x for x in events),'dock scan not blocked')
            check(any('|KNOWN|101|0|' in x for x in events),'known star not mapped on entry')
            check(any('|KNOWN|102|1|' in x for x in events),'known planet not mapped on entry')
            check(not any('|KNOWN|103|' in x or '|KNOWN|104|' in x for x in events),'hidden site leaked on entry')
            check(any('|D_DONE|' in x and int(x.rsplit('|',1)[-1])>=4 for x in events),'directional world contacts absent')
            check(any('|P_DONE|0' in x for x in events),'unprobed scan returned hidden sites')
            check(any('|P_DONE|2' in x for x in events),'probe scan missed actual hidden sites')
            check(any('|PUBLISH|2' in x for x in events),'explicit publish missing')
            check(any('|RECOVER|1' in x for x in events),'probe recovery missing')
            check(any('|ARCHIVE|103|' in x and x.endswith('|0') for x in events),'unpublished observation not shown privately')
            check(any('|ARCHIVE|103|' in x and x.endswith('|1') for x in events),'published state not shown in archive')
            check(stdout.count(b'SGVIEW|1|')==86 and stdout.count(b'END\n')==86,'SGVIEW damaged by survey sideband')
            with sqlite3.connect(db) as con:
                check(con.execute('select count(*) from observations where published=1').fetchone()[0]==2,
                      'published observations did not persist')
            print('PASS B6.1 native: authoritative dock rejection, live directional contacts, probes, private SQLite, explicit publication, recovery, intact SGVIEW')
        finally:
            if p.poll() is None:p.kill();p.wait(timeout=3)

def event_types(blob):
    surveys=[]
    for block in blob.split('\n\n'):
        lines=block.split('\n')
        if len(lines)==2 and lines[0]=='event: survey':
            surveys.append(base64.b64decode(lines[1].removeprefix('data: '),validate=True).decode('ascii'))
    return surveys

def relay(exe,server):
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    base=f'http://127.0.0.1:{port}'
    p=subprocess.Popen([sys.executable,str(server),'--host-exe',str(exe),'--port',str(port),
                        '--tick-ms','55','--max-ticks','65'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    try:
        for _ in range(120):
            try:
                if b'alive' in request(base,'/healthz'):break
            except (urllib.error.URLError,ConnectionError):time.sleep(.04)
        else:raise AssertionError('relay unavailable')
        info=json.loads(request(base,'/session'));token=info['token']
        check({'directional','probeDeploy','probeScan','probeRecover','surveyArchive','surveyPublish'}.issubset(info['commands']),
            'scanner actions missing from live capabilities')
        html=request(base,'/viewer/interactive.html')
        check(all(('id="'+x+'"').encode() in html for x in ['directional','probeDeploy','probeScan',
                 'probeRecover','surveyArchive','surveyPublish','surveyResults']),'scanner controls missing')
        check(b'GalaxySurveyContract' in request(base,'/viewer/survey.js'),'survey validator not served')
        good={'token':token}
        for doc,expected in [({**good,'action':'directional','bearing':360,'cone':30},400),
                             ({**good,'action':'directional','bearing':0,'cone':45},400),
                             ({**good,'action':'probeDeploy','position':[0,0]},400),
                             ({**good,'action':'probeScan','owner':999},400),
                             ({'token':'wrong','action':'probeRecover'},403)]:
            code,_=post(base,doc);check(code==expected,'bad scan request was not rejected: '+str(doc))
        code,_=post(base,{**good,'action':'probeScan'},origin=False)
        check(code==403,'cross-origin probe scan permitted')
        a=[];b=[]
        ta=threading.Thread(target=collect,args=(base,a),daemon=True);ta.start()
        code,_=post(base,{**good,'action':'undock'});check(code==202,'could not undock')
        time.sleep(.26)
        tb=threading.Thread(target=collect,args=(base,b),daemon=True);tb.start()
        for action,extra in [('directional',{'bearing':0,'cone':360}),('probeDeploy',{}),
                             ('probeScan',{}),('surveyArchive',{}),('surveyPublish',{}),
                             ('surveyArchive',{}),('probeRecover',{})]:
            code,_=post(base,{**good,'action':action,**extra});check(code==202,'could not submit '+action)
            time.sleep(.12)
        ta.join(12);tb.join(12)
        check(not ta.is_alive() and not tb.is_alive(),'live SSE never finished')
        fa,fb=extract_sse(a[0]),extract_sse(b[0]);check(len(fa)==66 and fa==fb,'world frames diverged')
        sa,sb=event_types(a[0]),event_types(b[0]);check(sa==sb,'survey replay diverged across viewers')
        check(any('|D_DONE|' in x for x in sa),'relay lost directional scan')
        check(any('|P_DONE|2' in x for x in sa),'relay lost probe discoveries')
        check(any('|PUBLISH|2' in x for x in sa),'relay lost publication receipt')
        late=request(base,'/events').decode('ascii')
        check(extract_sse(late)==fa and event_types(late)==sa,'late survey or world replay diverged')
        print('PASS B6.1 relay: token, same-origin and schema validation; two viewers + late replay of authoritative world and survey events')
    finally:
        p.terminate()
        try:p.communicate(timeout=3)
        except subprocess.TimeoutExpired:p.kill();p.communicate()

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--executable',required=True,type=pathlib.Path)
    ap.add_argument('--relay',required=True,type=pathlib.Path)
    args=ap.parse_args();native(args.executable.resolve());relay(args.executable.resolve(),args.relay.resolve())
