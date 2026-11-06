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
            for action,payload in [
                ("colonyPlanetCrew",{"token":token,"action":"colonyPlanetCrew"}),
                ("colonyPlanetStatus",{**valid,"action":"colonyPlanetStatus"}),
                ("colonyPlanetBuild",{**valid,"action":"colonyPlanetBuild",
                                      "firstNpc":21,"secondNpc":22}),
                ("colonyPlanetSupport",{**valid,"action":"colonyPlanetSupport",
                                        "resource":"FOOD","kg":10}),
                ("colonyPlanetPolicy",{**valid,"action":"colonyPlanetPolicy",
                                       "policy":1}),
            ]:
                assert action in session["commands"],action
                status,_=request(port,"POST","/command",payload)
                assert status==202,(action,status)
                status,_=request(port,"POST","/command",{**payload,"token":"forged"})
                assert status==403,(action,status)
            operations=[
                ("colonyPlanetBuild",{}),
                ("colonyPlanetEquip",{"equipment":"FARM"}),
                ("colonyPlanetEquip",{"equipment":"EXTRACT"}),
                ("colonyPlanetSupply",{"resource":"WATER","kg":100}),
                ("colonyPlanetGrow",{"batches":2}),
                ("colonyPlanetExtract",{"resource":"NUTRIENTS","kg":1000}),
                ("colonyPlanetRetrieve",{"resource":"FOOD","kg":12}),
                ("colonyPlanetSupport",{"resource":"OXYGEN","kg":10}),
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
            shuttle_base={'token':token,'action':'colonyPlanetShuttle',
                          'body':4294967300,'latitude':17,'longitude':36}
            assert 'colonyPlanetShuttle' in session['commands']
            shuttle_valid=[
                {'operation':'STATUS'}, {'operation':'BUILD'},
                {'operation':'PILOT','npc':42},
                {'operation':'BOARD','npc':43},
                {'operation':'RELEASE','npc':43},
                {'operation':'DISEMBARK','npc':43},
                {'operation':'REBOARD','npc':43},
                {'operation':'DUTY_ON','npc':43},
                {'operation':'DUTY_OFF','npc':43},
                {'operation':'LOAD','resource':'WATER','kg':100},
                {'operation':'UNLOAD','resource':'FOOD','kg':25},
                {'operation':'DESCEND','request':987654321},
                {'operation':'ASCEND','request':987654322}
            ]
            for details in shuttle_valid:
                payload={**shuttle_base,**details}
                status,_=request(port,'POST','/command',payload)
                assert status==202,(details,status)
                status,_=request(port,'POST','/command',
                    {**payload,'token':'forged'})
                assert status==403,(details,status)
            invalid_shuttles=[
                {'operation':'LOAD','resource':'WATER','kg':True},
                {'operation':'LOAD','resource':'OXYGEN','kg':100},
                {'operation':'LOAD','resource':'WATER','kg':1001},
                {'operation':'UNLOAD','resource':'FOOD','kg':-1},
                {'operation':'DESCEND','request':0},
                {'operation':'ASCEND','request':True},
                {'operation':'ASCEND','request':9007199254740992},
                {'operation':'BUILD','kg':1},
                {'operation':'STATUS','pilot':42},
                {'operation':'PILOT','npc':False},
                {'operation':'BOARD','npc':False},
                {'operation':'RELEASE','npc':0},
                {'operation':'DISEMBARK','npc':True},
                {'operation':'REBOARD','npc':0},
                {'operation':'DUTY_ON','npc':False},
                {'operation':'DUTY_OFF','npc':0},
                {'operation':'BOARD','npc':42,'kg':1},
                {'operation':'PILOT','npc':42,'extra':1},
                {'operation':'FREE'},
                {'operation':'STATUS','latitude':36},
                {'operation':'STATUS','longitude':72},
                {'operation':'STATUS','body':True}
            ]
            for details in invalid_shuttles:
                payload={**shuttle_base,**details}
                status,_=request(port,'POST','/command',payload)
                assert status==400,(details,status)
            for payload in [
                {**valid,"action":"colonyPlanetEquip","equipment":"SHUTTLE"},
                {**valid,"action":"colonyPlanetGrow","batches":True},
                {**valid,"action":"colonyPlanetGrow","batches":101},
                {**valid,"action":"colonyPlanetSupply","resource":"FOOD","kg":1},
                {**valid,"action":"colonyPlanetExtract","resource":"WATER","kg":500},
                {**valid,"action":"colonyPlanetBuild","firstNpc":42},
                {**valid,"action":"colonyPlanetBuild","firstNpc":42,"secondNpc":42},
                {**valid,"action":"colonyPlanetBuild","firstNpc":True,"secondNpc":43},
                {**valid,"action":"colonyPlanetStatus","latitude":-1},
                {**valid,"action":"colonyPlanetPolicy","policy":-1},
                {**valid,"action":"colonyPlanetPolicy","policy":True},
                {**valid,"action":"colonyPlanetPolicy","policy":5},
                {**valid,"action":"colonyPlanetPolicy","policy":2,"kg":1},
                {**valid,"action":"colonyPlanetSupport","resource":"NUTRIENTS","kg":10},
                {**valid,"action":"colonyPlanetRetrieve","resource":"NUTRIENTS","kg":1}
            ]:
                status,_=request(port,"POST","/command",payload)
                assert status==400,(payload,status)
            # Local NPC delivery-board contracts have narrow typed inputs.
            for action in ('colonyLocalWorkPost','colonyLocalWorkStatus',
                           'colonyLocalWorkCancel'):
                assert action in session['commands'],action
            posted={'token':token,'action':'colonyLocalWorkPost',
                    'body':4294967300,'latitude':17,'longitude':36,
                    'resource':'NUTRIENTS','kg':500,'order':77003}
            for payload in [
                posted,
                {'token':token,'action':'colonyLocalWorkStatus'},
                {'token':token,'action':'colonyLocalWorkCancel','order':77003}
            ]:
                status,_=request(port,'POST','/command',payload)
                assert status==202,(payload,status)
                status,_=request(port,'POST','/command',
                                 {**payload,'token':'forged'})
                assert status==403,(payload,status)
            for payload in [
                {**posted,'resource':'GOLD'},
                {**posted,'kg':1001},
                {**posted,'kg':True},
                {**posted,'order':0},
                {**posted,'order':1<<50},
                {**posted,'latitude':36},
                {**posted,'nonce':777},
                {'token':token,'action':'colonyLocalWorkCancel','order':True},
                {'token':token,'action':'colonyLocalWorkStatus','extra':1}
            ]:
                status,_=request(port,'POST','/command',payload)
                assert status==400,(payload,status)
            # One player instruction plans the local NPC pickup AND the
            # real second-carrier assignment atomically in native SQLite.
            combined={'token':token,'action':'colonyLocalWorkPostToColony',
                      'body':4294967300,'latitude':17,'longitude':36,
                      'resource':'NUTRIENTS','kg':200,'order':77007,
                      'carrier':3,'destination':4}
            assert 'colonyLocalWorkPostToColony' in session['commands']
            status,_=request(port,'POST','/command',combined)
            assert status==202
            status,_=request(port,'POST','/command',
                             {**combined,'token':'forged'})
            assert status==403
            for bad in (
                {**combined,'carrier':True},
                {**combined,'destination':3},
                {**combined,'resource':'GOLD'},
                {**combined,'kg':0},
                {**combined,'order':1<<50},
                {**combined,'longitude':72},
                {**combined,'nonce':1}
            ):
                status,_=request(port,'POST','/command',bad)
                assert status==400,(bad,status)
            # Real intercolony carriage has strict typed identities. HTTP
            # acknowledgement queues a native command; it is not delivery.
            for action in ('colonyFreightLoad','colonyFreightDispatch',
                           'colonyFreightDeliver','colonyFreightStatus'):
                assert action in session['commands'],action
            shipment={'token':token,'action':'colonyFreightLoad',
                      'order':77003,'carrier':3,'destination':4}
            for payload in (
                shipment,
                {'token':token,'action':'colonyFreightDispatch','order':77003},
                {'token':token,'action':'colonyFreightDeliver','order':77003},
                {'token':token,'action':'colonyFreightStatus'}
            ):
                status,_=request(port,'POST','/command',payload)
                assert status==202,(payload,status)
                status,_=request(port,'POST','/command',
                                 {**payload,'token':'forged'})
                assert status==403,(payload,status)
            for payload in (
                {**shipment,'carrier':4},
                {**shipment,'carrier':True},
                {**shipment,'carrier':0},
                {**shipment,'order':1<<50},
                {**shipment,'destination':0},
                {**shipment,'destination':True},
                {**shipment,'extra':1},
                {'token':token,'action':'colonyFreightDispatch','order':True},
                {'token':token,'action':'colonyFreightDeliver','order':0},
                {'token':token,'action':'colonyFreightStatus','order':77003}
            ):
                status,_=request(port,'POST','/command',payload)
                assert status==400,(payload,status)
            for action in ('colonyShipyardBuild','colonyShipyardAssign',
                           'colonyShipyardStatus'):
                assert action in session['commands'],action
            yard={'token':token,'action':'colonyShipyardBuild',
                  'order':99001,'firstNpc':101,'secondNpc':102}
            for payload in (yard,
                            {**yard,'action':'colonyShipyardAssign'},
                            {'token':token,'action':'colonyShipyardStatus'}):
                status,_=request(port,'POST','/command',payload)
                assert status==202,(payload,status)
                status,_=request(port,'POST','/command',
                                 {**payload,'token':'forged'})
                assert status==403,(payload,status)
            for payload in (
                {**yard,'order':True},
                {**yard,'order':0},
                {**yard,'order':1<<50},
                {**yard,'firstNpc':True},
                {**yard,'secondNpc':0},
                {**yard,'secondNpc':101},
                {**yard,'unexpected':'scope'},
                {'token':token,'action':'colonyShipyardStatus','order':99001}
            ):
                status,_=request(port,'POST','/command',payload)
                assert status==400,(payload,status)
            for action in ('colonyFreighterBuild','colonyFreighterCommission','colonyFreighterStatus'):
                assert action in session['commands'],action
            freighter={'token':token,'action':'colonyFreighterBuild',
                      'yard':91001,'order':92001}
            for payload in (
                freighter,
                {'token':token,'action':'colonyFreighterStatus'}
            ):
                status,_=request(port,'POST','/command',payload)
                assert status==202,(payload,status)
                status,_=request(port,'POST','/command',
                                 {**payload,'token':'forged'})
                assert status==403,(payload,status)
            for payload in (
                {**freighter,'yard':True},
                {**freighter,'order':True},
                {**freighter,'yard':0},
                {**freighter,'order':0},
                {**freighter,'order':91001},
                {**freighter,'order':1<<50},
                {**freighter,'extra':1},
                {'token':token,'action':'colonyFreighterStatus','order':92001}
            ):
                status,_=request(port,'POST','/command',payload)
                assert status==400,(payload,status)
            commissioning={'token':token,'action':'colonyFreighterCommission',
                           'order':92001,'firstNpc':103,'secondNpc':104,
                           'provisionsKg':20}
            status,_=request(port,'POST','/command',commissioning)
            assert status==202,status
            status,_=request(port,'POST','/command',
                             {**commissioning,'token':'forged'})
            assert status==403,status
            for payload in (
                {**commissioning,'order':True},
                {**commissioning,'order':1<<50},
                {**commissioning,'firstNpc':True},
                {**commissioning,'secondNpc':True},
                {**commissioning,'secondNpc':103},
                {**commissioning,'provisionsKg':True},
                {**commissioning,'provisionsKg':9},
                {**commissioning,'provisionsKg':1001},
                {**commissioning,'extra':123},
            ):
                status,_=request(port,'POST','/command',payload)
                assert status==400,(payload,status)
            pilot={'token':token,'action':'colonyFreighterPilotStatus','order':92001}
            thrust={'token':token,'action':'colonyFreighterThrust',
                    'order':92001,'x':1,'y':0}
            yaw={'token':token,'action':'colonyFreighterYaw',
                 'order':92001,'delta':15}
            travel={'token':token,'action':'colonyFreighterTravel',
                    'order':92001,'body':4294967300}
            for payload in (pilot,thrust,yaw,travel):
                status,_=request(port,'POST','/command',payload)
                assert status==202,(payload,status)
                status,_=request(port,'POST','/command',
                                 {**payload,'token':'forged'})
                assert status==403,(payload,status)
            for payload in (
                {**pilot,'order':True},
                {**pilot,'order':1<<50},
                {**pilot,'extra':1},
                {**thrust,'x':True},
                {**thrust,'x':2},
                {**thrust,'y':-2},
                {**thrust,'extra':1},
                {**yaw,'delta':True},
                {**yaw,'delta':90},
                {**travel,'body':False},
                {**travel,'body':0},
                {**travel,'body':1<<53},
                {**travel,'extra':1},
            ):
                status,_=request(port,'POST','/command',payload)
                assert status==400,(payload,status)
            # SHIP-01D: the relay accepts a requested owned ship ID, not
            # user-supplied principal, scanner, crew or resource authority.
            fleet={'token':token,'action':'colonyFleetStatus'}
            select={'token':token,'action':'colonyFleetSelect','ship':1}
            for payload in (fleet,select):
                status,_=request(port,'POST','/command',payload)
                assert status==202,(payload,status)
                status,_=request(port,'POST','/command',
                                 {**payload,'token':'forged'})
                assert status==403,(payload,status)
            for payload in (
                {**fleet,'ship':1},
                {**fleet,'extra':'yes'},
                {**select,'ship':True},
                {**select,'ship':0},
                {**select,'ship':-2},
                {**select,'ship':1<<53},
                {**select,'extra':1},
                {**select,'playerId':7},
            ):
                status,_=request(port,'POST','/command',payload)
                assert status==400,(payload,status)
            # SHIP-01E: true fleet dispatch has no client-supplied owner,
            # fabricated crew, cargo receipt or bypass flag. Strict shape.
            fleet_job={'token':token,'action':'colonyFleetWorkPost',
                       'order':77007,'carrier':4,'destination':5,
                       'body':4294967300,'latitude':17,'longitude':36,
                       'resource':'NUTRIENTS','kg':100}
            status,_=request(port,'POST','/command',fleet_job)
            assert status==202,status
            status,_=request(port,'POST','/command',
                             {**fleet_job,'token':'forged'})
            assert status==403,status
            for payload in (
                {**fleet_job,'order':True},
                {**fleet_job,'carrier':True},
                {**fleet_job,'carrier':0},
                {**fleet_job,'destination':4},
                {**fleet_job,'destination':False},
                {**fleet_job,'body':0},
                {**fleet_job,'latitude':36},
                {**fleet_job,'longitude':-1},
                {**fleet_job,'resource':'ORE'},
                {**fleet_job,'kg':0},
                {**fleet_job,'kg':1001},
                {**fleet_job,'owner':7},
                {**fleet_job,'onboardCrew':2},
                {**fleet_job,'skipTravel':True},
            ):
                status,_=request(port,'POST','/command',payload)
                assert status==400,(payload,status)
            # SHIP-01F: customer shipyard controls are exact authenticated
            # transitions; no user may inject claimed ownership, NPCs,
            # material balances, hull IDs or bypass physical berthing.
            for action,payload in (
                ('colonyYardContractPost',{'order':93001,'yard':91001,'build':92001}),
                ('colonyYardContractAccept',{'order':93001}),
                ('colonyYardContractFund',{'order':93001}),
                ('colonyYardContractDeliver',{'order':93001}),
                ('colonyYardContractStatus',{}),
            ):
                assert action in session['commands'],action
                valid_contract={'token':token,'action':action,**payload}
                status,_=request(port,'POST','/command',valid_contract)
                assert status==202,(action,status)
                status,_=request(port,'POST','/command',
                    {**valid_contract,'token':'forged'})
                assert status==403,(action,status)
                status,_=request(port,'POST','/command',
                    {**valid_contract,'claimedOwner':7})
                assert status==400,(action,status)
            template={'token':token,'action':'colonyYardContractPost',
                      'order':93001,'yard':91001,'build':92001}
            for payload in (
                {**template,'order':True},
                {**template,'yard':False},
                {**template,'build':0},
                {**template,'build':93001},
                {**template,'yard':93001},
                {**template,'order':1<<50},
                {**template,'modules':6},
                {**template,'freeHull':1234},
                {**template,'crew':[11,12]},
                {**template,'skipBerth':True},
            ):
                status,_=request(port,'POST','/command',payload)
                assert status==400,(payload,status)
            for action in ('colonyYardContractAccept','colonyYardContractFund',
                           'colonyYardContractDeliver'):
                for order in (0,-1,True,1<<50):
                    status,_=request(port,'POST','/command',
                                     {'token':token,'action':action,'order':order})
                    assert status==400,(action,order,status)
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
