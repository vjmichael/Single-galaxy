"""Native live host, restart, bound relay, served B7.0j UI (not browser click-through)."""
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
    with tempfile.TemporaryDirectory(prefix='sg_b70j_live_') as d:
        root=Path(d);db=root/'colony.sqlite'
        commands=b'\n'.join([
            b'MINE|40',b'REFINE|40',b'FABRICATE|20',b'BUILD',
            b'UTILITY_BUILD|7',b'HAB_BUILD|7',b'COMMUNITY_BUILD|7',
            b'HAB_STOCK|7|food|50',b'HAB_STOCK|7|water|50',
            b'HAB_STOCK|7|oxygen|50',b'HAB_RESIDENTS|7|1',
            b'COMMUNITY_STAFF|7|1',b'COMMUNITY_STOCK|7|food|15',
            b'COMMUNITY_STOCK|7|water|15'])+b'\n'
        run=subprocess.run([str(host),str(db),'20','95'],input=commands,capture_output=True,timeout=15)
        assert run.returncode==0,run.stderr[-1300:]
        text=run.stderr.decode('ascii')
        assert '|COMMUNITY|7|1|' in text,text[-1400:]
        assert '|COMMUNITY_ASSESS|1|1|1' in text,text[-1200:]
        assert '|RESILIENCE|' in text
        assert '|'+'SELF_SUFFICIENT' not in text
        restored=subprocess.run([str(host),str(db),'20','5'],capture_output=True,timeout=7)
        assert restored.returncode==0,restored.stderr[-1400:]
        assert '|RESUMED' in restored.stderr.decode('ascii')
        assert '|COMMUNITY|7|1|' in restored.stderr.decode('ascii')
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
            for action in ('colonyCommunityBuild','colonyCommunityStaff','colonyCommunityStock'):
                assert action in session['commands'],(action,session['commands'])
            base={'token':session['token']}
            for action,props in [
                ('colonyCommunityBuild',{'outpost':7}),
                ('colonyCommunityStaff',{'outpost':7,'amount':2}),
                ('colonyCommunityStock',{'outpost':7,'resource':'water','amount':25}),
                ('colonyCommunityStock',{'outpost':7,'resource':'food','amount':-2})
            ]:
                code,_=request(port,'POST','/command',{**base,'action':action,**props})
                assert code==202,(action,code)
            invalid=[
                ('colonyCommunityBuild',{'outpost':True}),
                ('colonyCommunityBuild',{'outpost':0}),
                ('colonyCommunityStaff',{'outpost':7,'amount':5}),
                ('colonyCommunityStaff',{'outpost':7,'amount':True}),
                ('colonyCommunityStock',{'outpost':7,'resource':'oxygen','amount':1}),
                ('colonyCommunityStock',{'outpost':7,'resource':'food','amount':0}),
                ('colonyCommunityStock',{'outpost':7,'resource':'water','amount':10001}),
                ('colonyCommunityStock',{'outpost':7,'resource':'water','amount':1,'issuer':7}),
            ]
            for action,props in invalid:
                code,_=request(port,'POST','/command',{**base,'action':action,**props})
                assert code==400,(action,props,code)
            code,_=request(port,'POST','/command',{**base,'action':'colonyCommunityBuild','outpost':7},False)
            assert code==403
            code,_=request(port,'POST','/command',{'token':'forged','action':'colonyCommunityBuild','outpost':7})
            assert code==403
            for path in ('/viewer/colony.js','/viewer/interactive.html'):
                code,data=request(port,'GET',path)
                assert code==200 and b'colonyCommunity' in data,(path,code)
        finally:
            server.terminate()
            try:server.communicate(timeout=5)
            except subprocess.TimeoutExpired:server.kill();server.communicate()
    print('B7.0j native host, community service, restored state, local relay security: PASS')
if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--host',type=Path,required=True)
    ap.add_argument('--relay',type=Path,required=True);args=ap.parse_args()
    main(args.host,args.relay)
