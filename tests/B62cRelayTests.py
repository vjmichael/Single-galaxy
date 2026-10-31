#!/usr/bin/env python3
"""B6.2c local HTTP relay exercises real jump UI command boundary."""
import argparse,base64,json,pathlib,socket,subprocess,sys,time,urllib.error,urllib.request,threading
from B52InteractiveEndToEndTests import request,post,extract_sse,check

def run(exe,relay):
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    base='http://127.0.0.1:'+str(port)
    proc=subprocess.Popen([sys.executable,str(relay),'--host-exe',str(exe),'--port',str(port),
                           '--tick-ms','50','--max-ticks','56'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    try:
        from RelayReadiness import wait_for_relay
        wait_for_relay(proc,lambda: b'alive' in request(base,'/healthz'),
                       label='B6.2c gate relay')
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
        # Subscribe before the undock command, and wait for its *committed*
        # SGVIEW transition rather than sleeping an assumed number of ticks.
        undocked=threading.Event()
        observer_error=[]
        def watch_undock():
            try:
                with urllib.request.urlopen(base+'/events',timeout=15) as stream:
                    for line in stream:
                        if not line.startswith(b'data: '):continue
                        try:frame=base64.b64decode(line[6:].strip(),validate=True)
                        except (ValueError,base64.binascii.Error):continue
                        if b'\nZ|2\n' in frame or b'\nZ|2|' in frame:
                            undocked.set();return
            except Exception as exc:
                observer_error.append(repr(exc))
        thread=threading.Thread(target=watch_undock,daemon=True)
        thread.start()
        status,_=post(base,{'token':token,'action':'undock'})
        check(status==202,'undock request not queued')
        check(undocked.wait(12),'authoritative undock state was not observed: '+str(observer_error))
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
