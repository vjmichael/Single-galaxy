"""B7.0c live host and local relay: accepted/rejected new inputs and real receipts."""
import argparse
import http.client
import json
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

def request(port, method, path, body=None, origin=True):
    conn=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
    payload=json.dumps(body).encode() if body is not None else None
    headers={'Content-Type':'application/json'} if body is not None else {}
    if body is not None and origin:headers['Origin']=f'http://127.0.0.1:{port}'
    conn.request(method,path,body=payload,headers=headers)
    response=conn.getresponse();result=response.status,response.read();conn.close();return result

def check(host,relay):
    with tempfile.TemporaryDirectory(prefix='b70c_host_') as tmp:
        db=Path(tmp)/'direct.sqlite'
        direct=subprocess.run([str(host),str(db),'20','18'],input=b'SCAN\nMINE_TARGET|6|8\nPROCESS|nickel_iron|1000\nFABRICATE_MATERIAL|alloy|1\n',capture_output=True,timeout=12)
        assert direct.returncode==0,direct.stderr[-500:]
        log=direct.stderr.decode('ascii')
        assert '|STOCK_CLEAR' in log and '|STOCK|TAILINGS|0' in log
        assert '|RESULT|' in log and '|OUT_OF_RANGE|0' in log,log[-1000:]
        assert '|NO_RESOURCES|0' in log,log[-1000:]
        assert b'SGVIEW|1|' in direct.stdout
        with socket.socket() as s:
            s.bind(('127.0.0.1',0));port=s.getsockname()[1]
        relay_server=subprocess.Popen([sys.executable,str(relay),'--host-exe',str(host),'--colony',
            '--save',str(Path(tmp)/'relay.sqlite'),'--port',str(port),'--tick-ms','20','--max-ticks','200'],
            stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            for _ in range(80):
                try:
                    status,data=request(port,'GET','/session')
                    if status==200:break
                except OSError:pass
                time.sleep(.025)
            else:raise AssertionError('local relay unavailable')
            session=json.loads(data)
            assert session['colony_mode'] and 'colonyProcess' in session['commands']
            token=session['token'];root={'token':token}
            for action,extra in [
                ('colonyMineTarget',{'target':6,'amount':8}),
                ('colonyProcess',{'mineral':'nickel_iron','kg':1000}),
                ('colonyFabricateMaterial',{'material':'alloy','amount':1}),
            ]:
                code,_=request(port,'POST','/command',{**root,'action':action,**extra})
                assert code==202,(action,code)
            for action,extra in [
                ('colonyMineTarget',{'target':True,'amount':8}),
                ('colonyProcess',{'mineral':'other','kg':1000}),
                ('colonyProcess',{'mineral':'nickel_iron','kg':-1}),
                ('colonyProcess',{'mineral':'plagioclase','kg':1,'extra':'tamper'}),
                ('colonyFabricateMaterial',{'material':'gold','amount':1}),
                ('colonyFabricateMaterial',{'material':'aluminum','amount':1.5}),
            ]:
                code,_=request(port,'POST','/command',{**root,'action':action,**extra})
                assert code==400,(action,extra,code)
            code,_=request(port,'POST','/command',{'token':'bad','action':'colonyProcess','mineral':'plagioclase','kg':1})
            assert code==403
            code,_=request(port,'POST','/command',{**root,'action':'colonyProcess','mineral':'plagioclase','kg':1},origin=False)
            assert code==403
            code,js=request(port,'GET','/viewer/colony.js')
            assert code==200 and b'colonyFabricateMaterial' in js
            print('PASS B7.0c: live host receipts + protected local mineral processing and fabrication relay')
        finally:
            relay_server.terminate()
            try:relay_server.communicate(timeout=4)
            except subprocess.TimeoutExpired:relay_server.kill();relay_server.communicate()

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--host',required=True,type=Path);parser.add_argument('--relay',required=True,type=Path)
    args=parser.parse_args();check(args.host,args.relay)
