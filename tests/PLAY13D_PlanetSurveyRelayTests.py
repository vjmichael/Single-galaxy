#!/usr/bin/env python3
"""PGF-01B.2: HTTP relay validates planetary survey authority and coordinates."""
import argparse
import http.client
import json
import socket
import subprocess
import sys
import tempfile
from pathlib import Path

def request(port,method,path,body=None,origin=True):
    conn=http.client.HTTPConnection("127.0.0.1",port,timeout=5)
    data=json.dumps(body).encode() if body is not None else None
    headers={"Content-Type":"application/json"} if body is not None else {}
    if data is not None and origin:
        headers["Origin"]=f"http://127.0.0.1:{port}"
    conn.request(method,path,body=data,headers=headers)
    response=conn.getresponse()
    result=response.status,response.read()
    conn.close()
    return result

def main(executable,relay):
    with tempfile.TemporaryDirectory(prefix="sg_pgf01b2_http_") as tmp:
        with socket.socket() as sock:
            sock.bind(("127.0.0.1",0))
            port=sock.getsockname()[1]
        proc=subprocess.Popen([
            sys.executable,str(relay),"--host-exe",str(executable),
            "--colony","--save",str(Path(tmp)/"world.sqlite"),
            "--port",str(port),"--tick-ms","20","--max-ticks","200"],
            stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            from RelayReadiness import wait_for_relay
            wait_for_relay(proc,lambda:request(port,"GET","/session")[0]==200,
                           label="PGF-01B.2 HTTP planetary relay")
            status,raw=request(port,"GET","/session")
            assert status==200
            session=json.loads(raw)
            assert "colonyPlanetSurvey" in session["commands"],session
            token=session["token"]
            valid={"token":token,"action":"colonyPlanetSurvey",
                   "body":4294967300,"latitude":17,"longitude":36}
            status,_=request(port,"POST","/command",valid)
            assert status==202,status
            for extra in [
                {"latitude":36},{"longitude":72},{"latitude":True},
                {"longitude":-1},{"body":0},{"body":True},
                {"body":9223372036854775808},{"unknown":12}
            ]:
                status,_=request(port,"POST","/command",{**valid,**extra})
                assert status==400,(extra,status)
            status,_=request(port,"POST","/command",
                             {**valid,"token":"forged"})
            assert status==403,status
            status,_=request(port,"POST","/command",valid,origin=False)
            assert status==403,status
            operations=[
                ("colonyPlanetBuild",{}),
                ("colonyPlanetEquip",{"equipment":"FARM"}),
                ("colonyPlanetEquip",{"equipment":"EXTRACT"}),
                ("colonyPlanetSupply",{"resource":"WATER","kg":100}),
                ("colonyPlanetGrow",{"batches":2}),
                ("colonyPlanetExtract",{"resource":"NUTRIENTS","kg":1000}),
                ("colonyPlanetRetrieve",{"resource":"FOOD","kg":12}),
            ]
            for action,details in operations:
                assert action in session["commands"],action
                payload={**valid,"action":action,**details}
                status,_=request(port,"POST","/command",payload)
                assert status==202,(action,status)
                for mutation in [
                    {"latitude":36},
                    {"longitude":72},
                    {"body":False},
                    {"unexpected":"forged"}
                ]:
                    status,_=request(port,"POST","/command",
                                     {**payload,**mutation})
                    assert status==400,(action,mutation,status)
                status,_=request(port,"POST","/command",
                                 {**payload,"token":"forged"})
                assert status==403,(action,status)
            for payload in [
                {**valid,"action":"colonyPlanetEquip","equipment":"SHUTTLE"},
                {**valid,"action":"colonyPlanetGrow","batches":True},
                {**valid,"action":"colonyPlanetGrow","batches":101},
                {**valid,"action":"colonyPlanetSupply","resource":"FOOD","kg":1},
                {**valid,"action":"colonyPlanetExtract","resource":"WATER","kg":500},
                {**valid,"action":"colonyPlanetRetrieve","resource":"NUTRIENTS","kg":1}
            ]:
                status,_=request(port,"POST","/command",payload)
                assert status==400,(payload,status)
            print("PASS PGF01B2 HTTP relay accepts valid geography and rejects forged inputs")
        finally:
            proc.terminate()
            try:proc.communicate(timeout=4)
            except subprocess.TimeoutExpired:
                proc.kill();proc.communicate()

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--host",type=Path,required=True)
    parser.add_argument("--relay",type=Path,required=True)
    args=parser.parse_args()
    main(args.host.resolve(),args.relay.resolve())
