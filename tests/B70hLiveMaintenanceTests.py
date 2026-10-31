"""B7.0h native C++ commands, local relay validation, and telemetry acceptance."""
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
    c=http.client.HTTPConnection('127.0.0.1',port,timeout=4)
    headers={'Content-Type':'application/json'} if data is not None else {}
    if data is not None and origin:headers['Origin']=f'http://127.0.0.1:{port}'
    c.request(method,path,json.dumps(data).encode() if data is not None else None,headers)
    res=c.getresponse();r=res.status,res.read();c.close();return r

def main(host,relay):
    with tempfile.TemporaryDirectory(prefix='sg_b70h_') as dirname:
        root=Path(dirname);db=root/'colony.sqlite'
        commands=b'\n'.join([
            b'MINE|40',b'REFINE|40',b'FABRICATE|20',b'BUILD',b'UTILITY_BUILD|7',
            b'HAB_BUILD|7',b'HAB_STOCK|7|food|20',b'HAB_STOCK|7|water|20',
            b'HAB_STOCK|7|oxygen|20',b'HAB_RESIDENTS|7|1',
            b'UTILITY_SPARES|7|2',b'UTILITY_STAFF|7|1',b'UTILITY_AUTO|7|1',
        ])+b'\n'
        run=subprocess.run([str(host),str(db),'20','100'],input=commands,capture_output=True,timeout=10)
        assert run.returncode==0,run.stderr[-1400:]
        log=run.stderr.decode('ascii')
        assert '|MAINTENANCE|7|2|1|1|' in log,log[-1800:]
        assert '|MAINTENANCE_ASSESS|' in log
        restored=subprocess.run([str(host),str(db),'20','6'],capture_output=True,timeout=5)
        assert restored.returncode==0,restored.stderr[-1200:]
        assert '|RESUMED' in restored.stderr.decode('ascii')
        assert '|MAINTENANCE|7|2|1|1|' in restored.stderr.decode('ascii')
        with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
        server=subprocess.Popen([sys.executable,str(relay),'--host-exe',str(host),'--colony',
          '--save',str(root/'relay.sqlite'),'--port',str(port),'--tick-ms','20','--max-ticks','200'],
          stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            for _ in range(120):
                try:
                    status,data=request(port,'GET','/session')
                    if status==200:break
                except OSError:pass
                time.sleep(.025)
            else:raise AssertionError('relay did not start')
            session=json.loads(data);assert session['colony_mode'];token=session['token']
            for action in ['colonyUtilitySpares','colonyUtilityStaff','colonyUtilityAuto']:
                assert action in session['commands']
            for action,amount in [('colonyUtilitySpares',2),('colonyUtilityStaff',1),
                                  ('colonyUtilityAuto',1),('colonyUtilityAuto',0)]:
                code,_=request(port,'POST','/command',{'token':token,'action':action,'outpost':7,'amount':amount})
                assert code==202,(action,amount,code)
            for action,amount in [('colonyUtilitySpares',0),('colonyUtilitySpares',33),
                                  ('colonyUtilityStaff',9),('colonyUtilityStaff',-1),
                                  ('colonyUtilityAuto',2),('colonyUtilityAuto',True)]:
                code,_=request(port,'POST','/command',{'token':token,'action':action,'outpost':7,'amount':amount})
                assert code==400,(action,amount,code)
            code,_=request(port,'POST','/command',{'token':token,'action':'colonyUtilityAuto','outpost':7})
            assert code==400
            code,_=request(port,'POST','/command',{'token':token,'action':'colonyUtilityAuto','outpost':7,'amount':1,'issuer':7})
            assert code==400
            code,_=request(port,'POST','/command',{'token':token,'action':'colonyUtilityAuto','outpost':7,'amount':1},False)
            assert code==403
            code,_=request(port,'POST','/command',{'token':'forged','action':'colonyUtilitySpares','outpost':7,'amount':1})
            assert code==403
            status,content=request(port,'GET','/viewer/colony.js')
            assert status==200 and b'MAINTENANCE_ASSESS' in content
        finally:
            server.terminate()
            try:server.communicate(timeout=5)
            except subprocess.TimeoutExpired:server.kill();server.communicate()
    print('B7.0h local host, live telemetry, relay schema and auth checks: PASS')

if __name__=='__main__':
    a=argparse.ArgumentParser();a.add_argument('--host',type=Path,required=True)
    a.add_argument('--relay',type=Path,required=True);v=a.parse_args();main(v.host,v.relay)
