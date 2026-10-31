#!/usr/bin/env python3
"""B6.2c local HTTP relay exercises real jump UI command boundary."""
import argparse,json,pathlib,socket,subprocess,sys,time,urllib.error
from B52InteractiveEndToEndTests import request,post,extract_sse,check

def run(exe,relay):
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    base='http://127.0.0.1:'+str(port)
    proc=subprocess.Popen([sys.executable,str(relay),'--host-exe',str(exe),'--port',str(port),
                           '--tick-ms','50','--max-ticks','56'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    try:
        for _ in range(100):
            try:
                if b'alive' in request(base,'/healthz'):break
            except (OSError,urllib.error.URLError):time.sleep(.04)
        else:raise AssertionError('relay not ready')
        session=json.loads(request(base,'/session'))
        check({'jump','travelGate'}.issubset(session['commands']),'gate controls missing')
        html=request(base,'/viewer/interactive.html')
        check(b'id="travelGate"' in html and b'id="jump"' in html,'browser gate buttons missing')
        token=session['token']
        for bad in [{'token':token,'action':'jump','gate':5},
                    {'token':token,'action':'travelGate','target':5}]:
            status,_=post(base,bad)
            check(status==400,'browser was able to supply a raw gate target')
        status,_=post(base,{'token':'wrong','action':'jump'})
        check(status==403,'invalid token accepted')
        status,_=post(base,{'token':token,'action':'jump'},origin=False)
        check(status==403,'cross-origin gate command accepted')
        status,_=post(base,{'token':token,'action':'undock'})
        check(status==202,'undock request not queued')
        time.sleep(.24)
        status,_=post(base,{'token':token,'action':'jump'})
        check(status==202,'valid gate action rejected by relay')
        event_raw=request(base,'/events').decode('ascii')
        stream=extract_sse(event_raw)
        check(len(stream)==57,'missing authoritative relay frames')
        systems=[int(x.split('\n',1)[0].split('|')[6]) for x in stream]
        check(1 in systems and 2 in systems,'relay did not deliver destination state')
        snapshots=[(i,int(f.split('\n',1)[0].split('|')[6])) for i,f in enumerate(stream)
                   if f.startswith('SGVIEW|1|1|')]
        check(len(snapshots)==2 and [x[1] for x in snapshots]==[1,2],
              'system change was not a complete scoped snapshot: '+str(snapshots))
        print('PASS B6.2c relay: safe jump commands, forbidden target selection, 57 authoritative frames, destination snapshot')
    finally:
        proc.terminate()
        try:proc.communicate(timeout=3)
        except subprocess.TimeoutExpired:proc.kill();proc.communicate()

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--executable',type=pathlib.Path,required=True)
    p.add_argument('--relay',type=pathlib.Path,required=True)
    a=p.parse_args();run(a.executable,a.relay)
