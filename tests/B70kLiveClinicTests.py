"""B7.0k native host, SQLite restart, relay allowlist and UI assets (not Chromium)."""
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
    r=c.getresponse();out=r.status,r.read();c.close();return out

def main(host,relay):
    with tempfile.TemporaryDirectory(prefix='sg_b70k_live_') as d:
        root=Path(d);db=root/'colony.sqlite'
        commands=b'\n'.join([
            b'MINE|40',b'REFINE|40',b'FABRICATE|20',b'BUILD',
            b'UTILITY_BUILD|7',b'HAB_BUILD|7',b'CLINIC_BUILD|7',
            b'HAB_STOCK|7|food|60',b'HAB_STOCK|7|water|60',
            b'HAB_STOCK|7|oxygen|60',b'HAB_RESIDENTS|7|1',
            b'CLINIC_STAFF|7|1',b'CLINIC_STOCK|7|water|15',
            b'CLINIC_STOCK|7|substrate|15'])+b'\n'
        run=subprocess.run([str(host),str(db),'20','95'],input=commands,capture_output=True,timeout=16)
        assert run.returncode==0,run.stderr[-1500:]
        text=run.stderr.decode('ascii')
        assert '|CLINIC|7|1|' in text,text[-1600:]
        assert '|CLINIC_ASSESS|1|1|0|0|1' in text,text[-1600:]
        assert '|RESILIENCE|' in text and '|COMMUNITY_ASSESS|' in text
        assert 'SELF_SUFFICIENT' not in text
        restored=subprocess.run([str(host),str(db),'20','5'],capture_output=True,timeout=8)
        assert restored.returncode==0,restored.stderr[-1400:]
        assert '|RESUMED' in restored.stderr.decode('ascii')
        assert '|CLINIC|7|1|' in restored.stderr.decode('ascii')
        with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
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
            for action in ('colonyClinicBuild','colonyClinicStaff','colonyClinicStock'):
                assert action in session['commands'],(action,session['commands'])
            base={'token':session['token']}
            for action,props in [
                ('colonyClinicBuild',{'outpost':7}),
                ('colonyClinicStaff',{'outpost':7,'amount':2}),
                ('colonyClinicStock',{'outpost':7,'resource':'water','amount':15}),
                ('colonyClinicStock',{'outpost':7,'resource':'substrate','amount':-2})
            ]:
                code,_=request(port,'POST','/command',{**base,'action':action,**props})
                assert code==202,(action,code)
            for action,props in [
                ('colonyClinicBuild',{'outpost':True}),
                ('colonyClinicBuild',{'outpost':0}),
                ('colonyClinicStaff',{'outpost':7,'amount':5}),
                ('colonyClinicStaff',{'outpost':7,'amount':True}),
                ('colonyClinicStock',{'outpost':7,'resource':'oxygen','amount':1}),
                ('colonyClinicStock',{'outpost':7,'resource':'water','amount':0}),
                ('colonyClinicStock',{'outpost':7,'resource':'substrate','amount':10001}),
                ('colonyClinicStock',{'outpost':7,'resource':'water','amount':1,'issuer':7})
            ]:
                code,_=request(port,'POST','/command',{**base,'action':action,**props})
                assert code==400,(action,props,code)
            code,_=request(port,'POST','/command',{**base,'action':'colonyClinicBuild','outpost':7},False)
            assert code==403
            code,_=request(port,'POST','/command',{'token':'forged','action':'colonyClinicBuild','outpost':7})
            assert code==403
            for path in ('/viewer/colony.js','/viewer/interactive.html'):
                code,data=request(port,'GET',path)
                assert code==200 and b'colonyClinic' in data,(path,code)
        finally:
            server.terminate()
            try:server.communicate(timeout=5)
            except subprocess.TimeoutExpired:server.kill();server.communicate()
    print('B7.0k native host, real clinic service, restart, local relay security: PASS')
if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--host',type=Path,required=True)
    ap.add_argument('--relay',type=Path,required=True);args=ap.parse_args()
    main(args.host,args.relay)
