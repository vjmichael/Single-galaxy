"""B7.0d authoritative live crop / local-relay validation, no visual Chromium claim."""
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
    connection=http.client.HTTPConnection('127.0.0.1',port,timeout=5)
    headers={'Content-Type':'application/json'} if data is not None else {}
    if data is not None and origin:headers['Origin']=f'http://127.0.0.1:{port}'
    connection.request(method,route,body=json.dumps(data).encode() if data is not None else None,headers=headers)
    response=connection.getresponse()
    output=(response.status,response.read());connection.close();return output


def main(host,relay):
    with tempfile.TemporaryDirectory(prefix='sg_b70d_live_') as folder:
        root=Path(folder); db=root/'host.sqlite'
        # Two durable simulation sessions avoid sleeping an assumed number of
        # wall-clock ticks. The first session plants; the second proves crops
        # matured and survived a persisted restart before harvest.
        planted=subprocess.run([str(host),str(db),'20','24'],input=b'PLANT|20\n',
                               capture_output=True,timeout=15)
        assert planted.returncode==0,planted.stderr[-800:]
        harvested=subprocess.run([str(host),str(db),'20','45'],input=b'HARVEST|20\n',
                                 capture_output=True,timeout=15)
        assert harvested.returncode==0,harvested.stderr[-800:]
        assert b'|RESUMED' in harvested.stderr,'harvest did not resume persisted world'
        log=(planted.stderr+harvested.stderr).decode('ascii')
        assert '|LIFE|' in log and '|PLANT' not in log
        assert log.count('|APPLIED|')>=2,log[-1400:]
        assert '|STOCK|' in log
        last=[x for x in log.splitlines() if '|LIFE|' in x][-1].split('|')
        assert len(last)==17 and int(last[4])>0,last
        # State persisted: reopening the save must not reset crew provisioning or harvest.
        resumed=subprocess.run([str(host),str(db),'20','5'],capture_output=True,timeout=8)
        assert resumed.returncode==0,resumed.stderr[-800:]
        saved=resumed.stderr.decode('ascii')
        assert '|RESUMED' in saved
        restored=[x for x in saved.splitlines() if '|LIFE|' in x][0].split('|')
        assert restored[4]==last[4],(restored,last)
        # Test the browser relay allowlist, schema, origin and token boundaries.
        with socket.socket() as sock:
            sock.bind(('127.0.0.1',0)); port=sock.getsockname()[1]
        relay_server=subprocess.Popen([sys.executable,str(relay),'--host-exe',str(host),'--colony',
            '--save',str(root/'relay.sqlite'),'--port',str(port),'--tick-ms','20','--max-ticks','150'],
            stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            from RelayReadiness import wait_for_relay
            wait_for_relay(relay_server, lambda: request(port,'GET','/session')[0] == 200,
                           label='B7.0d agricultural relay')
            code,body=request(port,'GET','/session')
            doc=json.loads(body); assert doc['colony_mode']
            assert 'colonyPlant' in doc['commands'] and 'colonyOxygen' in doc['commands']
            token=doc['token'];permit={'token':token}
            for action,n in [('colonyPlant',20),('colonyHarvest',20),('colonyRecycleWater',20),
                             ('colonyRecycleCompost',20),('colonyOxygen',5),('colonyLifeWater',1)]:
                code,_=request(port,'POST','/command',{**permit,'action':action,'amount':n})
                assert code==202,(action,code)
            for action,n in [('colonyPlant',True),('colonyHarvest',0),('colonyOxygen',1.5),
                             ('colonyRecycleWater',10001),('colonyLifeWater',101)]:
                code,_=request(port,'POST','/command',{**permit,'action':action,'amount':n})
                assert code==400,(action,code)
            code,_=request(port,'POST','/command',{**permit,'action':'colonyPlant','amount':5,'issuer':999})
            assert code==400
            code,_=request(port,'POST','/command',{**permit,'action':'colonyPlant','amount':5},origin=False)
            assert code==403
            code,_=request(port,'POST','/command',{'token':'bad','action':'colonyPlant','amount':5})
            assert code==403
            code,js=request(port,'GET','/viewer/colony.js')
            assert code==200 and b'lifeFood' in js
        finally:
            relay_server.terminate()
            try:relay_server.communicate(timeout=4)
            except subprocess.TimeoutExpired:relay_server.kill();relay_server.communicate()
    print('PASS B7.0d live maturation, persisted harvest, secure relay input and viewer assets')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--host',required=True,type=Path)
    parser.add_argument('--relay',required=True,type=Path)
    args=parser.parse_args();main(args.host,args.relay)
