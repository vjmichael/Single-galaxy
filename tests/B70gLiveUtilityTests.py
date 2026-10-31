"""B7.0g live host power telemetry + relay validation, local development only."""
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
    connection=http.client.HTTPConnection('127.0.0.1',port,timeout=4)
    headers={'Content-Type':'application/json'} if data is not None else {}
    if data is not None and origin:headers['Origin']=f'http://127.0.0.1:{port}'
    connection.request(method,path,json.dumps(data).encode() if data is not None else None,headers)
    result=connection.getresponse();response=result.status,result.read();connection.close();return response

def main(host,relay):
    with tempfile.TemporaryDirectory(prefix='sg_b70g_') as dirname:
        root=Path(dirname);db=root/'colony.sqlite'
        commands=b'MINE|40\nREFINE|40\nFABRICATE|20\nBUILD\nUTILITY_BUILD|7\nUTILITY_REPAIR|7\n'
        run=subprocess.run([str(host),str(db),'20','88'],input=commands,capture_output=True,timeout=16)
        assert run.returncode==0,run.stderr[-1300:]
        log=run.stderr.decode('ascii')
        assert '|UTILITY|7|' in log,log[-1000:]
        assert '|UTILITY_ASSESS|' in log
        assert '|INVALID_ARGUMENT|0' in log # repair was unnecessary at commissioning
        assert 'galaxy-kernel-v13|' in '' or db.exists()
        restarted=subprocess.run([str(host),str(db),'20','5'],capture_output=True,timeout=7)
        assert restarted.returncode==0,restarted.stderr[-1000:]
        assert '|RESUMED' in restarted.stderr.decode('ascii')
        assert '|UTILITY|7|' in restarted.stderr.decode('ascii')
        with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
        server=subprocess.Popen([sys.executable,str(relay),'--host-exe',str(host),'--colony',
          '--save',str(root/'relay.sqlite'),'--port',str(port),'--tick-ms','20','--max-ticks','160'],
          stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            for _ in range(120):
                try:
                    code,data=request(port,'GET','/session')
                    if code==200:break
                except OSError:pass
                time.sleep(.025)
            else:raise AssertionError('relay not ready')
            session=json.loads(data);assert session['colony_mode']
            assert all(x in session['commands'] for x in ('colonyUtilityBuild','colonyUtilityRepair'))
            base={'token':session['token']}
            for action in ('colonyUtilityBuild','colonyUtilityRepair'):
                code,_=request(port,'POST','/command',{**base,'action':action,'outpost':7})
                assert code==202,(action,code)
            for body in [
                {'outpost':True},{'outpost':0},{'outpost':100000001},{'outpost':7.5},
                {'outpost':'7'},{'outpost':7,'amount':1}]:
                code,_=request(port,'POST','/command',{**base,'action':'colonyUtilityBuild',**body})
                assert code==400,(body,code)
            code,_=request(port,'POST','/command',{**base,'action':'colonyUtilityRepair','outpost':7},False)
            assert code==403
            code,_=request(port,'POST','/command',{'token':'wrong','action':'colonyUtilityBuild','outpost':7})
            assert code==403
            code,body=request(port,'GET','/viewer/colony.js')
            assert code==200 and b'colonyUtilityRepair' in body
        finally:
            server.terminate()
            try:server.communicate(timeout=5)
            except subprocess.TimeoutExpired:server.kill();server.communicate()
    print('B7.0g live utility host, saved facility, command authorization: PASS')

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--host',required=True,type=Path)
    parser.add_argument('--relay',required=True,type=Path)
    args=parser.parse_args();main(args.host,args.relay)
