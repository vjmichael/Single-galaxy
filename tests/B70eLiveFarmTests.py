"""B7.0e live physical farm construction, local relay schema and restart."""
import argparse
import http.client
import json
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def request(port, method, route, data=None, origin=True):
    con=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
    headers={'Content-Type':'application/json'} if data is not None else {}
    if data is not None and origin:headers['Origin']=f'http://127.0.0.1:{port}'
    con.request(method,route,body=json.dumps(data).encode() if data is not None else None,headers=headers)
    response=con.getresponse();ret=response.status,response.read();con.close();return ret


def main(host,relay):
    with tempfile.TemporaryDirectory(prefix='sg_b70e_live_') as folder:
        base=Path(folder);save=base/'colony.sqlite'
        commands=(b'MINE|40\nREFINE|40\nFABRICATE|20\nBUILD\nFARM_BUILD|7\n'
                  b'FARM_STAFF|7|1\n' # correctly fails until stocked
                  b'FARM_STOCK|7|food|200\nFARM_STOCK|7|water|900\n'
                  b'FARM_STOCK|7|oxygen|200\nFARM_STOCK|7|nutrients|200\n'
                  b'FARM_STOCK|7|co2|200\nFARM_STAFF|7|8\n')
        run=subprocess.run([str(host),str(save),'20','55'],input=commands,
                           capture_output=True,timeout=15)
        assert run.returncode==0,run.stderr[-1000:]
        stderr=run.stderr.decode('ascii')
        assert '|OUTPOST|7|1' in stderr,stderr[-900:]
        assert '|FARM|7|8|' in stderr,stderr[-900:]
        assert '|ASSESS|24|8|' in stderr,stderr[-900:]
        assert '|NO_RESOURCES|0' in stderr,stderr[-900:]
        assert '|APPLIED|4' in stderr
        last=[x for x in stderr.splitlines() if '|FARM|7|' in x][-1].split('|')
        assert int(last[12])>0, last  # harvested_food
        restart=subprocess.run([str(host),str(save),'20','5'],capture_output=True,timeout=8)
        assert restart.returncode==0,restart.stderr[-1000:]
        text=restart.stderr.decode('ascii')
        assert '|RESUMED' in text and '|FARM|7|8|' in text
        with socket.socket() as sock:
            sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
        server=subprocess.Popen([sys.executable,str(relay),'--host-exe',str(host),'--colony',
            '--save',str(base/'relay.sqlite'),'--port',str(port),'--tick-ms','20','--max-ticks','160'],
            stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            for _ in range(120):
                try:
                    code,data=request(port,'GET','/session')
                    if code==200:break
                except OSError:pass
                time.sleep(.025)
            else:raise AssertionError('relay not available')
            session=json.loads(data)
            assert session['colony_mode']
            assert all(x in session['commands'] for x in ('colonyFarmBuild','colonyFarmStock','colonyFarmStaff'))
            user={'token':session['token']}
            good=[('colonyFarmBuild',{'outpost':7}),
                  ('colonyFarmStock',{'outpost':7,'resource':'water','amount':900}),
                  ('colonyFarmStaff',{'outpost':7,'amount':1}),
                  ('colonyFarmStock',{'outpost':7,'resource':'food','amount':-10})]
            for action,params in good:
                code,_=request(port,'POST','/command',{**user,'action':action,**params})
                assert code==202,(action,code)
            malformed=[('colonyFarmBuild',{'outpost':True}),
                       ('colonyFarmStaff',{'outpost':7,'amount':9}),
                       ('colonyFarmStaff',{'outpost':7,'amount':0}),
                       ('colonyFarmStock',{'outpost':7,'resource':'aluminum','amount':10}),
                       ('colonyFarmStock',{'outpost':7,'resource':'water','amount':10001}),
                       ('colonyFarmStock',{'outpost':7,'resource':'food','amount':1.5}),
                       ('colonyFarmStock',{'outpost':7,'resource':'food','amount':10,'issuer':7})]
            for action,params in malformed:
                code,_=request(port,'POST','/command',{**user,'action':action,**params})
                assert code==400,(action,code)
            code,_=request(port,'POST','/command',{**user,'action':'colonyFarmBuild','outpost':7},origin=False)
            assert code==403
            code,_=request(port,'POST','/command',{'token':'wrong','action':'colonyFarmBuild','outpost':7})
            assert code==403
            code,asset=request(port,'GET','/viewer/colony.js')
            assert code==200 and b'colonyFarmSupply' in asset
        finally:
            server.terminate()
            try:server.communicate(timeout=5)
            except subprocess.TimeoutExpired:server.kill();server.communicate()
    print('B7.0e live host, farm operations, resume, authenticated relay and malformed inputs: PASS')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--host',type=Path,required=True)
    parser.add_argument('--relay',type=Path,required=True)
    a=parser.parse_args();main(a.host,a.relay)
