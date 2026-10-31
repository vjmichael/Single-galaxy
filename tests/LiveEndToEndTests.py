#!/usr/bin/env python3
"""B4 development-only process/SSE verification. No external packages."""
import argparse
import base64
import pathlib
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request


def check(condition, reason):
    if not condition: raise AssertionError(reason)


def run(executable, relay):
    with tempfile.TemporaryDirectory(prefix='galaxy-live-e2e-') as tmp:
        for scenario,expected in [('ten',(10,100,0)),('hold',(7,10,10)),('motion',(12,None,None))]:
            path=pathlib.Path(tmp)/(scenario+'.db')
            started=time.monotonic()
            result=subprocess.run([str(executable),scenario,str(path),'20'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=20)
            elapsed=time.monotonic()-started
            check(result.returncode==0, 'host failure '+result.stderr.decode(errors='replace'))
            data=result.stdout.decode('ascii')
            check(data.count('SGVIEW|1|')==expected[0]+1,'unexpected frame count')
            check(data.count('\nEND\n')==expected[0]+1,'broken frame endings')
            check(data.splitlines()[0].startswith('SGVIEW|1|1|'),'missing opening snapshot')
            check('B4 LIVE PASS' in result.stderr.decode(),'no final status')
            check(elapsed>=expected[0]*.020*.7,'scheduler did not wait for time')
            if scenario=='ten':
                check('D|' in data,'no entity despawn delta')
            if scenario=='hold':
                check('A|3|2|' in data,'no mining stopped event')
            print('PASS native '+scenario+' frames='+str(expected[0]+1)+' elapsed='+format(elapsed,'.3f')+' s')

    with socket.socket() as s:
        s.bind(('127.0.0.1',0)); port=s.getsockname()[1]
    proc=subprocess.Popen(['python3',str(relay),'--host-exe',str(executable),'--port',str(port),'--tick-ms','20'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    try:
        health='http://127.0.0.1:'+str(port)+'/healthz'
        for attempt in range(40):
            try:
                with urllib.request.urlopen(health,timeout=.5) as r:
                    check(r.read().startswith(b'B4 loopback'),'health check')
                    break
            except (OSError,urllib.error.URLError): time.sleep(.05)
        else: raise AssertionError('relay failed to start')
        with urllib.request.urlopen('http://127.0.0.1:'+str(port)+'/viewer/live.html',timeout=3) as r:
            check(b'live.js' in r.read(),'live browser file missing')
        with urllib.request.urlopen('http://127.0.0.1:'+str(port)+'/events?scenario=hold',timeout=8) as r:
            check(r.headers.get_content_type()=='text/event-stream','expected SSE')
            payload=r.read().decode('ascii')
        records=payload.split('\n\n')
        frames=[]
        for chunk in records:
            if not chunk.strip(): continue
            lines=chunk.split('\n')
            if lines[0]=='event: frame':
                check(len(lines)==2 and lines[1].startswith('data: '),'bad SSE framing')
                frame=base64.b64decode(lines[1][6:],validate=True).decode('ascii')
                check(frame.startswith('SGVIEW|1|') and frame.endswith('END\n'),'invalid frame on relay')
                frames.append(frame)
        check(len(frames)==8,'expected 8 committed live frames')
        check(any('A|3|2|' in s for s in frames),'missing cargo-full notification')
        check('event: complete\ndata: finished' in payload,'missing end event')
        try:
            urllib.request.urlopen('http://127.0.0.1:'+str(port)+'/events?scenario=bad',timeout=3)
            raise AssertionError('relay accepted unrecognized scenario')
        except urllib.error.HTTPError as e:
            check(e.code==400,'wrong scenario rejection status')
        print('PASS SSE relay hold snapshots/deltas/cargo-full + static page + invalid scenario rejection')
    finally:
        proc.terminate()
        try: proc.communicate(timeout=3)
        except subprocess.TimeoutExpired: proc.kill();proc.communicate()

if __name__=='__main__':
    a=argparse.ArgumentParser()
    a.add_argument('--executable',type=pathlib.Path,required=True)
    a.add_argument('--relay',type=pathlib.Path,required=True)
    opts=a.parse_args()
    run(opts.executable.resolve(),opts.relay.resolve())
