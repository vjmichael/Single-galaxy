"""B7.0k-2 real C++/SQLite/reclaimer telemetry and HTTP relay command security."""
import argparse
import http.client
import json
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

def request(port,method,path,data=None,origin=True):
    conn=http.client.HTTPConnection('127.0.0.1',port,timeout=4)
    headers={'Content-Type':'application/json'} if data is not None else {}
    if data is not None and origin:headers['Origin']=f'http://127.0.0.1:{port}'
    conn.request(method,path,json.dumps(data).encode() if data is not None else None,headers)
    res=conn.getresponse();out=(res.status,res.read());conn.close();return out

def main(host,relay):
    with tempfile.TemporaryDirectory(prefix='sg_b70k_reclaimer_') as d:
        root=Path(d);db=root/'colony.sqlite'
        commands=b'\n'.join([b'MINE|40',b'REFINE|40',b'FABRICATE|20',b'BUILD',
            b'UTILITY_BUILD|7',b'FARM_BUILD|7',b'HAB_BUILD|7',b'RECLAIMER_BUILD|7',
            b'HAB_STOCK|7|food|100',b'HAB_STOCK|7|water|100',b'HAB_STOCK|7|oxygen|100',
            b'HAB_RESIDENTS|7|4',b'FARM_STOCK|7|food|100',b'FARM_STOCK|7|water|100',
            b'FARM_STOCK|7|oxygen|100',b'FARM_STOCK|7|nutrients|100',b'FARM_STOCK|7|co2|100',
            b'FARM_STAFF|7|2'])+b'\n'
        run=subprocess.run([str(host),str(db),'20','110'],input=commands,capture_output=True,timeout=20)
        assert run.returncode==0,run.stderr[-2000:]
        frames=run.stderr.decode('ascii')
        assert '|RECLAIMER|7|' in frames,frames[-2400:]
        assert '|LIFE_RECOVERY|1|1|' in frames,frames[-2400:]
        assert '|RESILIENCE|' in frames
        assert 'SELF_SUFFICIENT' not in frames
        restored=subprocess.run([str(host),str(db),'20','5'],capture_output=True,timeout=8)
        assert restored.returncode==0,restored.stderr[-1400:]
        resumed=restored.stderr.decode('ascii')
        assert '|RESUMED' in resumed and '|RECLAIMER|7|' in resumed,resumed[-1400:]
        with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
        server=subprocess.Popen([sys.executable,str(relay),'--host-exe',str(host),'--colony',
            '--save',str(root/'relay.sqlite'),'--port',str(port),'--tick-ms','20',
            '--max-ticks','180'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            for _ in range(130):
                try:
                    status,payload=request(port,'GET','/session')
                    if status==200:break
                except OSError:pass
                time.sleep(.025)
            else:raise AssertionError('relay not ready')
            session=json.loads(payload);assert session['colony_mode']
            assert 'colonyReclaimerBuild' in session['commands']
            base={'action':'colonyReclaimerBuild','token':session['token']}
            status,_=request(port,'POST','/command',{**base,'outpost':7});assert status==202,status
            for patch in [{'outpost':True},{'outpost':0},{'outpost':-1},
                          {'outpost':100000001},{'outpost':'7'},
                          {'outpost':7,'issuer':7},{'outpost':7,'amount':1}]:
                status,_=request(port,'POST','/command',{**base,**patch})
                assert status==400,(patch,status)
            assert request(port,'POST','/command',{**base,'outpost':7},False)[0]==403
            assert request(port,'POST','/command',{'token':'forged','action':'colonyReclaimerBuild','outpost':7})[0]==403
            for url in ('/viewer/colony.js','/viewer/interactive.html'):
                status,body=request(port,'GET',url)
                assert status==200 and b'colonyReclaimer' in body,(url,status)
        finally:
            server.terminate()
            try:server.communicate(timeout=5)
            except subprocess.TimeoutExpired:server.kill();server.communicate()
    print('B7.0k-2 native reclamation host, persisted telemetry, relay schema/origin/token defenses: PASS')

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--host',type=Path,required=True)
    ap.add_argument('--relay',type=Path,required=True);args=ap.parse_args();main(args.host,args.relay)
