"""Real colony C++ host, persisted habitat records and authenticated local relay."""
import argparse
import http.client
import json
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def request(port, method, path, data=None, origin=True):
    con=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
    headers={'Content-Type':'application/json'} if data is not None else {}
    if data is not None and origin: headers['Origin']=f'http://127.0.0.1:{port}'
    con.request(method,path,body=json.dumps(data).encode() if data is not None else None,headers=headers)
    res=con.getresponse();out=res.status,res.read();con.close();return out


def main(host,relay):
    with tempfile.TemporaryDirectory(prefix='sg_b70f_live_') as d:
        root=Path(d);db=root/'colony.sqlite'
        commands=(b'MINE|40\nREFINE|40\nFABRICATE|20\nBUILD\nHAB_BUILD|7\n'
                  b'HAB_RESIDENTS|7|3\n' # must fail until stocked
                  b'HAB_STOCK|7|food|250\nHAB_STOCK|7|water|300\nHAB_STOCK|7|oxygen|250\n'
                  b'HAB_RESIDENTS|7|3\nFARM_BUILD|7\n'
                  b'FARM_STOCK|7|food|160\nFARM_STOCK|7|water|700\n'
                  b'FARM_STOCK|7|oxygen|160\nFARM_STOCK|7|nutrients|160\n'
                  b'FARM_STOCK|7|co2|160\nFARM_STAFF|7|5\n')
        run=subprocess.run([str(host),str(db),'20','115'],input=commands,capture_output=True,timeout=20)
        assert run.returncode==0,run.stderr[-1500:]
        log=run.stderr.decode('ascii')
        assert '|HABITAT|7|3|8|8|' in log,log[-1300:]
        assert '|FARM|7|5|' in log,log[-1300:]
        assert '|HAB_ASSESS|3|8|8|0|1|LIVABLE' in log,log[-1400:]
        assert '|NO_RESOURCES|0' in log,log[-900:]
        restarted=subprocess.run([str(host),str(db),'20','5'],capture_output=True,timeout=8)
        assert restarted.returncode==0,restarted.stderr[-700:]
        text=restarted.stderr.decode('ascii')
        assert '|RESUMED' in text and '|HABITAT|7|3|8|8|' in text
        with socket.socket() as s:
            s.bind(('127.0.0.1',0));port=s.getsockname()[1]
        server=subprocess.Popen([sys.executable,str(relay),'--host-exe',str(host),'--colony',
            '--save',str(root/'relay.sqlite'),'--port',str(port),'--tick-ms','20','--max-ticks','160'],
            stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            for _ in range(120):
                try:
                    status,payload=request(port,'GET','/session')
                    if status==200:break
                except OSError:pass
                time.sleep(.025)
            else:raise AssertionError('relay not ready')
            session=json.loads(payload)
            assert session['colony_mode']
            assert all(x in session['commands'] for x in ('colonyHabBuild','colonyHabStock','colonyHabResidents'))
            user={'token':session['token']}
            for action,params in [('colonyHabBuild',{'outpost':7}),
                ('colonyHabStock',{'outpost':7,'resource':'water','amount':300}),
                ('colonyHabResidents',{'outpost':7,'amount':1}),
                ('colonyHabStock',{'outpost':7,'resource':'food','amount':-10})]:
                code,_=request(port,'POST','/command',{**user,'action':action,**params})
                assert code==202,(action,code)
            invalid=[('colonyHabBuild',{'outpost':True}),
              ('colonyHabResidents',{'outpost':7,'amount':9}),
              ('colonyHabResidents',{'outpost':7,'amount':0}),
              ('colonyHabStock',{'outpost':7,'resource':'aluminum','amount':10}),
              ('colonyHabStock',{'outpost':7,'resource':'food','amount':10001}),
              ('colonyHabStock',{'outpost':7,'resource':'food','amount':1.5}),
              ('colonyHabStock',{'outpost':7,'resource':'food','amount':1,'issuer':7})]
            for action,params in invalid:
                code,_=request(port,'POST','/command',{**user,'action':action,**params})
                assert code==400,(action,code)
            code,_=request(port,'POST','/command',{**user,'action':'colonyHabBuild','outpost':7},origin=False)
            assert code==403
            code,_=request(port,'POST','/command',{'token':'bad','action':'colonyHabBuild','outpost':7})
            assert code==403
            code,asset=request(port,'GET','/viewer/colony.js')
            assert code==200 and b'colonyHabSupply' in asset
        finally:
            server.terminate()
            try:server.communicate(timeout=5)
            except subprocess.TimeoutExpired:server.kill();server.communicate()
    print('B7.0f live housing, stored occupants, rescue, relay authorization and malformed inputs: PASS')

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--host',type=Path,required=True)
    ap.add_argument('--relay',type=Path,required=True)
    a=ap.parse_args();main(a.host,a.relay)
