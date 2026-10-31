"""B7.0i complete live-host telemetry + served relay assets, not a Chromium click run."""
import argparse
import http.client
import json
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

def request(port,path):
    c=http.client.HTTPConnection('127.0.0.1',port,timeout=4)
    c.request('GET',path)
    r=c.getresponse();out=(r.status,r.read());c.close();return out

def main(host,relay):
    with tempfile.TemporaryDirectory(prefix='sg_b70i_') as work:
        db=Path(work)/'resilience.sqlite'
        commands=b'\n'.join([b'MINE|40',b'REFINE|40',b'FABRICATE|20',b'BUILD',
           b'UTILITY_BUILD|7',b'HAB_BUILD|7',b'HAB_STOCK|7|food|20',
           b'HAB_STOCK|7|water|20',b'HAB_STOCK|7|oxygen|20',b'HAB_RESIDENTS|7|1'])+b'\n'
        run=subprocess.run([str(host),str(db),'20','90'],input=commands,capture_output=True,timeout=14)
        assert run.returncode==0,run.stderr[-1000:]
        lines=[v for v in run.stderr.decode('ascii').splitlines() if '|RESILIENCE|' in v]
        assert lines,run.stderr[-900:]
        last=lines[-1].split('|');assert len(last)==16,last
        assert int(last[-2])==0,'must NOT claim autonomous growth'
        mask=int(last[-1]);assert mask&256 and mask&512 and mask&1024
        assert mask&32,'site with no maintenance must be blocked'
        # Saved colony is authoritative after restart and telemetry remains derived.
        again=subprocess.run([str(host),str(db),'20','5'],capture_output=True,timeout=8)
        assert again.returncode==0,again.stderr[-1000:]
        assert '|RESUMED' in again.stderr.decode('ascii')
        assert '|RESILIENCE|' in again.stderr.decode('ascii')
        with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
        server=subprocess.Popen([sys.executable,str(relay),'--host-exe',str(host),
          '--colony','--save',str(Path(work)/'relay.sqlite'),'--port',str(port),
          '--tick-ms','20','--max-ticks','150'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            for _ in range(120):
                try:
                    status,session=request(port,'/session')
                    if status==200:break
                except OSError:pass
                time.sleep(.025)
            else:raise AssertionError('relay startup failed')
            session=json.loads(session)
            assert session['colony_mode']
            for name in ('/viewer/colony.js','/viewer/interactive.html'):
                status,payload=request(port,name)
                assert status==200 and b'colonyResilience' in payload
        finally:
            server.terminate()
            try:server.communicate(timeout=5)
            except subprocess.TimeoutExpired:server.kill();server.communicate()
    print('B7.0i native host, persisted diagnostic, served UI/relay: PASS')

if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--host',type=Path,required=True)
    p.add_argument('--relay',type=Path,required=True)
    a=p.parse_args();main(a.host,a.relay)
