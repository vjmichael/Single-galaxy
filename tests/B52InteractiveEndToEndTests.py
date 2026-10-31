#!/usr/bin/env python3
"""B5.2 authoritative ship/station lifecycle through direct process and loopback relay."""
import argparse
import base64
import json
import pathlib
import socket
import sys
import subprocess
import tempfile
import threading
import time
import urllib.request
import urllib.error


def check(ok, reason):
    if not ok: raise AssertionError(reason)


def parse_frames(blob):
    return ['SGVIEW|1|'+part for part in blob.split('SGVIEW|1|')[1:]]


def records(frames):
    return [line for frame in frames for line in frame.splitlines() if line.startswith(('A|','C|','Q|','Z|','H|'))]


def direct(exe):
    with tempfile.TemporaryDirectory(prefix='b52-direct-') as folder:
        db=pathlib.Path(folder)/'world.sqlite'
        proc=subprocess.Popen([str(exe),str(db),'45','65'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        # Start docked, exit, mine to capacity, dock while stopped, unload to local hangar.
        for command, pause in [('UNDOCK',.26),('MINE|4',.41),('DOCK',.25),('UNLOAD|1000000000',.06)]:
            proc.stdin.write((command+'\n').encode('ascii'))
            proc.stdin.flush()
            time.sleep(pause)
        output,errors=proc.communicate(timeout=12)
        check(proc.returncode==0,'native host failed '+errors.decode(errors='replace'))
        frames=parse_frames(output.decode('ascii'))
        check(len(frames)==66,'expected initial snapshot + 65 persisted deltas')
        rr=records(frames)
        check('Q|2|1|1|0' in rr,'initial dock state missing')
        check('H|1|7|0|100' in rr,'initial private hangar missing')
        check('Z|2' in rr,'undock state removal missing')
        check(any(r.startswith('A|') and '|3|0|2|1|0|0' in r for r in rr),'undock event missing')
        check(any(r.startswith('A|') and '|4|0|2|1|0|0' in r for r in rr),'dock event missing')
        check('H|1|7|20|100' in rr,'unload did not reach private hangar')
        check(any(r.startswith('C|') and r.endswith('|0|20') for r in rr),'transfer command receipt missing')
        check(db.stat().st_size>1000,'SQLite database is empty')
        last=rr[-1]
        check(last.startswith('C|') or last.startswith('H|'),'unexpected final trace')
        print('PASS native end-to-end: docked -> undock -> mine 20 -> dock -> unload 20 into private hangar')
        return frames


def request(base,path):
    with urllib.request.urlopen(base+path,timeout=8) as response:return response.read()


def post(base,doc,origin=True):
    headers={'Content-Type':'application/json'}
    if origin:headers['Origin']=base
    req=urllib.request.Request(base+'/command',data=json.dumps(doc,separators=(',',':')).encode('ascii'),headers=headers,method='POST')
    try:
        with urllib.request.urlopen(req,timeout=5) as response:return response.status,json.loads(response.read())
    except urllib.error.HTTPError as ex:return ex.code,json.loads(ex.read())


def collect(base,output):
    with urllib.request.urlopen(base+'/events',timeout=18) as response:
        output.append(response.read().decode('ascii'))


def extract_sse(payload):
    frames=[]
    for block in payload.split('\n\n'):
        lines=block.split('\n')
        if len(lines)==2 and lines[0]=='event: frame':
            frames.append(base64.b64decode(lines[1][6:],validate=True).decode('ascii'))
    return frames


def relay(exe, relay_path):
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0)); port=sock.getsockname()[1]
    base=f'http://127.0.0.1:{port}'
    proc=subprocess.Popen([sys.executable,str(relay_path),'--host-exe',str(exe),'--port',str(port),'--tick-ms','70','--max-ticks','68'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    try:
        for _ in range(90):
            try:
                check(b'interactive relay alive' in request(base,'/healthz'),'relay unhealthy')
                break
            except (urllib.error.URLError,ConnectionError):time.sleep(.04)
        else:raise AssertionError('relay did not start')
        session=json.loads(request(base,'/session'));token=session['token']
        check({'undock','dock','unload'}.issubset(session['commands']),'station command capabilities not advertised')
        html=request(base,'/viewer/interactive.html')
        check(b'id="undock"' in html and b'id="dock"' in html and b'id="unload"' in html, 'UI buttons absent')
        check(b'dockState' in html and b'hangarOre' in html,'station telemetry absent')
        good={'token':token}
        # Client-provided identities, targets and amounts MUST be rejected at HTTP boundary.
        for doc,expect in [({'token':token,'action':'dock','actor':42},400),
                          ({'token':token,'action':'unload','amount':100000},400),
                          ({'token':'evil','action':'undock'},403)]:
            code,_=post(base,doc);check(code==expect,'unauthorized or malformed station request accepted')
        code,_=post(base,{'token':token,'action':'undock'},origin=False)
        check(code==403,'cross-origin station request accepted')
        a=[];b=[]
        ta=threading.Thread(target=collect,args=(base,a),daemon=True);ta.start()
        code,_=post(base,{**good,'action':'undock'});check(code==202,'undock POST not queued')
        # An immediate docking attempt must be rejected by the authoritative model as IN_TRANSITION.
        code,_=post(base,{**good,'action':'dock'});check(code==202,'dock POST not queued')
        time.sleep(.43)
        tb=threading.Thread(target=collect,args=(base,b),daemon=True);tb.start()
        code,_=post(base,{**good,'action':'mine','amount':4});check(code==202,'mine not queued')
        time.sleep(.52)
        code,_=post(base,{**good,'action':'dock'});check(code==202,'dock not queued')
        time.sleep(.29)
        code,_=post(base,{**good,'action':'unload'});check(code==202,'unload not queued')
        ta.join(15);tb.join(15)
        check(not ta.is_alive() and not tb.is_alive(),'SSE replay hung')
        fa,fb=extract_sse(a[0]),extract_sse(b[0]);check(len(fa)==69 and fa==fb,'replayed viewers diverged')
        check(extract_sse(request(base,'/events').decode('ascii'))==fa,'late subscriber diverged')
        rr=records(fa)
        check(any('|11|0' in x and x.startswith('C|') for x in rr),'expected early IN_TRANSITION result')
        check(any(x.startswith('H|1|7|20|100') for x in rr),'no authoritative hangar transfer')
        check(any(x.startswith('C|') and x.endswith('|0|20') for x in rr),'no committed unload receipt')
        code,_=post(base,{**good,'action':'unload'});check(code==409,'commands accepted after finished')
        print('PASS relay security: token + same origin + schema constraints; authoritative early-transition denial')
        print('PASS two observers and late subscriber: identical committed station/hangar states')
    finally:
        proc.terminate()
        try:proc.communicate(timeout=3)
        except subprocess.TimeoutExpired:proc.kill();proc.communicate()

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--executable',required=True,type=pathlib.Path);ap.add_argument('--relay',required=True,type=pathlib.Path)
    args=ap.parse_args();direct(args.executable.resolve());relay(args.executable.resolve(),args.relay.resolve())
