"""B7.0m actual C++ host and HTTP relay policy execution contract."""
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
    with tempfile.TemporaryDirectory(prefix='sg_b70m_policy_') as d:
        root=Path(d);db=root/'colony.sqlite'
        commands=b'\n'.join([b'MINE|40',b'REFINE|40',b'FABRICATE|20',b'BUILD',
            b'UTILITY_BUILD|7',b'HAB_BUILD|7',b'HAB_STOCK|7|food|100',
            b'HAB_STOCK|7|water|100',b'HAB_STOCK|7|oxygen|100',
            b'HAB_RESIDENTS|7|5',b'POLICY_SET|7|0|3',b'POLICY_STAFF|7|0|3'])+b'\n'
        run=subprocess.run([str(host),str(db),'20','110'],input=commands,capture_output=True,timeout=25)
        assert run.returncode==0,run.stderr[-1800:]
        frames=run.stderr.decode('ascii')
        assert '|POLICY_AUDIT|7|0|3|3|3|0|1|0' in frames,frames[-2500:]
        assert '|RESILIENCE|' in frames
        restored=subprocess.run([str(host),str(db),'20','5'],capture_output=True,timeout=8)
        assert restored.returncode==0,restored.stderr[-1800:]
        resumed=restored.stderr.decode('ascii')
        assert '|RESUMED' in resumed and '|POLICY_AUDIT|7|0|3|3|3|0|1|0' in resumed,resumed[-1800:]
        with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
        server=subprocess.Popen([sys.executable,str(relay),'--host-exe',str(host),'--colony',
            '--save',str(root/'relay.sqlite'),'--port',str(port),'--tick-ms','20',
            '--max-ticks','180'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            for _ in range(140):
                try:
                    status,payload=request(port,'GET','/session')
                    if status==200:break
                except OSError:pass
                time.sleep(.025)
            else:raise AssertionError('relay not ready')
            session=json.loads(payload);assert session['colony_mode']
            assert 'colonyPolicySet' in session['commands'] and 'colonyPolicyStaff' in session['commands']
            base={'token':session['token'],'action':'colonyPolicySet','outpost':7,'domain':0,'amount':3}
            assert request(port,'POST','/command',base)[0]==202
            for bad in [
              {'outpost':True},{'outpost':'7'},{'outpost':0},{'domain':True},
              {'domain':4},{'domain':-1},{'amount':4},{'amount':-1},
              {'amount':True},{'amount':'3'},{'issuer':7}
            ]:
                status,_=request(port,'POST','/command',{**base,**bad})
                assert status==400,(bad,status)
            assert request(port,'POST','/command',base,False)[0]==403
            assert request(port,'POST','/command',{**base,'token':'forged'})[0]==403
            assert request(port,'POST','/command',{**base,'action':'colonyPolicyStaff','amount':2})[0]==202
            for url in ('/viewer/colony.js','/viewer/interactive.html'):
                status,body=request(port,'GET',url)
                assert status==200 and b'colonyPolicy' in body,(url,status)
        finally:
            server.terminate()
            try:server.communicate(timeout=5)
            except subprocess.TimeoutExpired:server.kill();server.communicate()
    print('B7.0m host policy v19 persistence, local authenticated relay boundary: PASS')

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--host',type=Path,required=True)
    ap.add_argument('--relay',type=Path,required=True);args=ap.parse_args();main(args.host,args.relay)
