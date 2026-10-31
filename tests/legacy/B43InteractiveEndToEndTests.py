#!/usr/bin/env python3
"""B4.3 native process + local command relay + multi-client reconnect verification."""
import argparse
import base64
import json
import pathlib
import socket
import subprocess
import tempfile
import threading
import time
import urllib.error
import urllib.request

def require(test,msg):
    if not test:raise AssertionError(msg)

def get(url):
    with urllib.request.urlopen(url,timeout=8) as r:return r.read()

def post(base,session,doc,origin=True):
    raw=json.dumps(doc,separators=(',',':')).encode('ascii')
    headers={'Content-Type':'application/json'}
    if origin:headers['Origin']=base
    req=urllib.request.Request(base+'/command',data=raw,headers=headers,method='POST')
    try:
        with urllib.request.urlopen(req,timeout=4) as r:return r.status,json.loads(r.read())
    except urllib.error.HTTPError as e:return e.code,json.loads(e.read())

def collect(url,output):
    with urllib.request.urlopen(url,timeout=15) as response:
        require(response.headers.get_content_type()=='text/event-stream','not SSE')
        output.append(response.read().decode('ascii'))

def frames(payload):
    result=[]
    for block in payload.split('\n\n'):
        lines=block.split('\n')
        if len(lines)==2 and lines[0]=='event: frame':
            result.append(base64.b64decode(lines[1].removeprefix('data: '),validate=True).decode('ascii'))
    return result

def run(host,relay):
    with tempfile.TemporaryDirectory(prefix='b4-interactive-e2e-') as t:
        db=pathlib.Path(t)/'native.sqlite'
        native=subprocess.Popen([str(host),str(db),'30','26'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        native.stdin.write(b'MINE|4\n');native.stdin.flush()
        out,err=native.communicate(timeout=12)
        require(native.returncode==0,err.decode(errors='replace'))
        require(db.exists() and db.stat().st_size>1000,'native database not committed')
        raw=out.decode('ascii')
        require(raw.count('SGVIEW|1|')==27,'wrong direct-host frame count')
        require('C|' in raw and 'A|' in raw,'missing mining command/activity')
        require('A|5|2|' in raw,'auto mining cargo full did not occur at tick 5')
        print('PASS direct C++ host: 26 committed ticks, mining+hold-full, nonempty SQLite database')

    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    base='http://127.0.0.1:'+str(port)
    proc=subprocess.Popen(['python3',str(relay),'--host-exe',str(host),'--port',str(port),'--tick-ms','65','--max-ticks','45'],
        stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    try:
        for attempt in range(80):
            try:
                require(b'interactive relay' in get(base+'/healthz'),'bad health')
                break
            except (OSError,urllib.error.URLError):time.sleep(.04)
        else:raise AssertionError('relay not started')
        sess=json.loads(get(base+'/session'));token=sess['token']
        require(len(token)>30,'weak token')
        require(b'interactive.js' in get(base+'/viewer/interactive.html'),'not interactive page')
        require(b'interpolatePositions' in get(base+'/viewer/interpolation.js'),'no interpolation')
        status,_=post(base,token,{'token':token,'action':'mine','amount':4},origin=False)
        require(status==403,'cross-origin/no-origin POST accepted')
        status,_=post(base,token,{'token':'wrong','action':'mine','amount':4})
        require(status==403,'wrong token accepted')
        status,_=post(base,token,{'token':token,'action':'thrust','x':True,'y':0})
        require(status==400,'boolean thrust accepted')
        status,_=post(base,token,{'token':token,'action':'mine','amount':999})
        require(status==400,'invalid mining rate accepted')
        status,_=post(base,token,{'token':token,'action':'mine','amount':4,'issuer':999})
        require(status==400,'client-supplied issuer accepted')
        # First observer begins as commands are enqueued. Additional observers join later.
        first=[];second=[]
        thread=threading.Thread(target=collect,args=(base+'/events',first),daemon=True);thread.start()
        status,_=post(base,token,{'token':token,'action':'mine','amount':4})
        require(status==202,'mine not queued')
        time.sleep(.35)
        second_thread=threading.Thread(target=collect,args=(base+'/events',second),daemon=True);second_thread.start()
        status,_=post(base,token,{'token':token,'action':'thrust','x':1,'y':0})
        require(status==202,'thrust not queued')
        status,_=post(base,token,{'token':token,'action':'stop'})
        require(status==202,'stop not queued')
        thread.join(12);second_thread.join(12)
        require(not thread.is_alive() and not second_thread.is_alive(),'SSE client hung')
        a,b=frames(first[0]),frames(second[0]);require(len(a)==46 and len(b)==46,'missing live/replay frames')
        require(a==b,'two spectators saw divergent worlds')
        require(a[0].startswith('SGVIEW|1|1|'),'no initial snapshot')
        require('event: complete' in first[0] and 'event: complete' in second[0],'missing completion')
        changes=''.join(a)
        require('A|' in changes and 'C|' in changes,'no command/results through relay')
        require('CARGO_FULL' not in changes,'wire format incorrectly carries textual label')
        require(any('|7|' in line for line in changes.splitlines() if line.startswith('A|') and '|2|' in line),
                'no cargo-full activity after mining')
        # A fresh connection after completion sees the same world, not a new simulation.
        late=frames(get(base+'/events').decode('ascii'))
        require(late==a,'late reconnection created or diverged from shared world')
        status,_=post(base,token,{'token':token,'action':'mine','amount':4})
        require(status==409,'command accepted after process completion')
        print('PASS loopback relay: same persistent world across spectators and late reconnect')
        print('PASS command policy: same-origin + token, schema/issuer/rate validation, ended-run denial')
        print('PASS live frames: snapshots, deltas, cargo-full and command receipts')
    finally:
        proc.terminate()
        try:proc.communicate(timeout=3)
        except subprocess.TimeoutExpired:proc.kill();proc.communicate()

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--executable',required=True,type=pathlib.Path)
    p.add_argument('--relay',required=True,type=pathlib.Path)
    a=p.parse_args();run(a.executable.resolve(),a.relay.resolve())
