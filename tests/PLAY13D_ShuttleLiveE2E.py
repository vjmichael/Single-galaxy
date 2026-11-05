#!/usr/bin/env python3
"""PLAY-13D.4C.5: real SQLite fixture -> authenticated relay -> native shuttle round trip -> restart."""
import argparse
import http.client
import json
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

def request(port, method, path, payload=None, token=None):
    conn=http.client.HTTPConnection("127.0.0.1",port,timeout=5)
    raw=json.dumps(payload).encode() if payload is not None else None
    headers={"Content-Type":"application/json",
             "Origin":f"http://127.0.0.1:{port}"} if raw is not None else {}
    conn.request(method,path,body=raw,headers=headers)
    r=conn.getresponse()
    status,body=r.status,r.read()
    conn.close()
    return status,json.loads(body) if body else {}

class EventReader:
    def __init__(self,port):
        self.records=[]
        self.condition=threading.Condition()
        self.finished=False
        self.error=None
        self.conn=http.client.HTTPConnection("127.0.0.1",port,timeout=30)
        self.conn.request("GET","/events")
        self.response=self.conn.getresponse()
        if self.response.status!=200:raise AssertionError("SSE rejected")
        self.thread=threading.Thread(target=self.read,daemon=True)
        self.thread.start()
    def read(self):
        event=""
        try:
            while True:
                raw=self.response.readline()
                if not raw:break
                line=raw.decode("utf-8","replace").strip()
                if line.startswith("event: "):
                    event=line[7:]
                elif line.startswith("data: "):
                    if event=="colony" and line[6:].startswith("COLONY|"):
                        fields=line[6:].split("|")
                        with self.condition:
                            self.records.append(fields)
                            self.condition.notify_all()
                    elif event=="failed":
                        with self.condition:self.error=line[6:]
                elif not line:
                    if event in ("complete","failed"):break
                    event=""
        except (OSError,TimeoutError) as exc:
            with self.condition:self.error=str(exc)
        finally:
            with self.condition:
                self.finished=True
                self.condition.notify_all()
    def cursor(self):
        with self.condition:return len(self.records)
    def wait(self, typ, after, predicate=lambda fields:True, timeout=8):
        until=time.monotonic()+timeout
        with self.condition:
            while True:
                for fields in self.records[after:]:
                    if len(fields)>3 and fields[3]==typ and predicate(fields):
                        return fields
                    if len(fields)>4 and fields[3]=="ERROR":
                        raise AssertionError("native rejected operation: "+"|".join(fields))
                if self.error:raise AssertionError("relay SSE failed: "+self.error)
                remaining=until-time.monotonic()
                if remaining<=0 or self.finished:
                    raise AssertionError(f"missing committed {typ} after={after}: {self.records[after:][-12:]}")
                self.condition.wait(remaining)
    def close(self):
        self.conn.close()

def choose_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1",0))
        return sock.getsockname()[1]

def launch(relay,host,database,port,max_ticks=100):
    return subprocess.Popen([sys.executable,str(relay),"--host-exe",str(host),
        "--colony","--save",str(database),"--port",str(port),
        "--tick-ms","100","--max-ticks",str(max_ticks)],
        stdout=subprocess.PIPE,stderr=subprocess.PIPE)

def session(port,proc):
    end=time.monotonic()+12
    while time.monotonic()<end:
        if proc.poll() is not None:
            out,err=proc.communicate()
            raise AssertionError("relay exited early: "+err.decode(errors="replace")[-2500:])
        try:
            status,doc=request(port,"GET","/session")
            if status==200:return doc
        except (OSError,ValueError):pass
        time.sleep(.05)
    raise AssertionError("relay did not become ready")

def send(port,reader,base,action,details=None,timeout=8):
    payload={**base,"operation":action,**(details or {})}
    cursor=reader.cursor()
    status,data=request(port,"POST","/command",payload)
    if status!=202:
        raise AssertionError(f"relay denied {action}: {status}, {data}")
    if action!="STATUS":
        reader.wait("PLANET_SHUTTLE_COMMITTED",cursor,lambda f:f[7]==action,timeout)
    status_record=reader.wait("PLANET_SHUTTLE_STATUS",cursor,
        lambda f:len(f)==17 and int(f[4])==base["body"],timeout)
    return status_record

def wait_phase(port,reader,base,phase,timeout=8):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        cursor=reader.cursor()
        status,info=request(port,"POST","/command",{**base,"operation":"STATUS"})
        if status!=202:raise AssertionError(f"status request failed: {status} {info}")
        report=reader.wait("PLANET_SHUTTLE_STATUS",cursor,timeout=2)
        if int(report[8])==phase:return report
        time.sleep(.1)
    raise AssertionError(f"flight phase {phase} never committed")

def main(fixture,host,relay):
    with tempfile.TemporaryDirectory(prefix="sg_play13d4c5_") as tmp:
        db=Path(tmp)/"world.sqlite"
        prep=subprocess.run([str(fixture),str(db)],capture_output=True,text=True,
                            timeout=45,check=True)
        body,latitude,longitude,ship,pilot,initial_mass,passenger=map(int,prep.stdout.strip().split())
        assert body>0 and pilot>0 and initial_mass>0
        port=choose_port()
        proc=launch(relay,host,db,port)
        try:
            info=session(port,proc)
            assert "colonyPlanetShuttle" in info["commands"]
            base={"token":info["token"],"action":"colonyPlanetShuttle",
                  "body":body,"latitude":latitude,"longitude":longitude}
            reader=EventReader(port)
            try:
                initial=send(port,reader,base,"STATUS")
                assert initial[7:9]==["0","0"],initial
                for invalid in (
                    {**base,"operation":"LOAD","resource":"WATER","kg":True},
                    {**base,"operation":"DESCEND","request":0},
                    {**base,"operation":"BUILD","owner":8},
                ):
                    status,_=request(port,"POST","/command",invalid)
                    assert status==400,(invalid,status)
                status,_=request(port,"POST","/command",
                    {**base,"operation":"BUILD","token":"forged"})
                assert status==403,status
                built=send(port,reader,base,"BUILD")
                assert built[7]=="1" and built[8]=="0",built
                selected=send(port,reader,base,"PILOT",{"npc":pilot})
                assert int(selected[9])==pilot,selected
                booked=send(port,reader,base,"BOARD",{"npc":passenger})
                assert int(booked[9])==pilot,booked
                loaded=send(port,reader,base,"LOAD",{"resource":"WATER","kg":200})
                assert int(loaded[10])==200,loaded
                loaded=send(port,reader,base,"LOAD",{"resource":"FOOD","kg":100})
                assert int(loaded[11])==100,loaded
                descent=send(port,reader,base,"DESCEND",{"request":81001})
                assert int(descent[14])==81001,descent
                arrived=wait_phase(port,reader,base,2)
                assert int(arrived[14])==81001,arrived
                visit=send(port,reader,base,"DISEMBARK",{"npc":passenger})
                assert int(visit[8])==2,visit
                # A real physical reboarding precedes the return launch.
                boarded_again=send(port,reader,base,"REBOARD",{"npc":passenger})
                assert int(boarded_again[8])==2,boarded_again
                unloaded=send(port,reader,base,"UNLOAD",{"resource":"WATER","kg":60})
                assert int(unloaded[10])==139,unloaded # 1 kg launch fuel
                unloaded=send(port,reader,base,"UNLOAD",{"resource":"FOOD","kg":20})
                assert int(unloaded[11])==80,unloaded
                loaded=send(port,reader,base,"LOAD",{"resource":"WATER","kg":50})
                assert int(loaded[10])==189,loaded
                ascent=send(port,reader,base,"ASCEND",{"request":81002})
                assert int(ascent[15])==81002,ascent
                orbit=wait_phase(port,reader,base,0)
                assert int(orbit[10])==188 and int(orbit[11])==80,orbit
                unloaded=send(port,reader,base,"UNLOAD",{"resource":"WATER","kg":50})
                assert int(unloaded[10])==138,unloaded
                # The host must reject replay even after completed arrival.
                cursor=reader.cursor()
                status,_=request(port,"POST","/command",
                    {**base,"operation":"DESCEND","request":81001})
                assert status==202,status
                rejected=reader.wait("ERROR",cursor,
                    lambda f:len(f)>4 and f[4]=="PLANET_SHUTTLE_DENIED")
                assert rejected
                assert wait_phase(port,reader,base,0)[10]=="138"
                # The native host finishes cleanly and releases its SQLite writer
                # lease before a second host process opens the same persistent world.
                with reader.condition:
                    until=time.monotonic()+15
                    while not reader.finished and time.monotonic()<until:
                        reader.condition.wait(.25)
                assert reader.finished and not reader.error,reader.error
            finally:reader.close()
        finally:
            proc.terminate()
            try:proc.communicate(timeout=4)
            except subprocess.TimeoutExpired:proc.kill();proc.communicate()
        # Real second process reopens saved world through the identical
        # authenticated native host and relay, not a direct SQLite shortcut.
        port=choose_port()
        proc=launch(relay,host,db,port,max_ticks=25)
        try:
            info=session(port,proc)
            base["token"]=info["token"]
            reader=EventReader(port)
            try:
                restored=send(port,reader,base,"STATUS")
                assert int(restored[8])==0 and int(restored[9])==pilot,restored
                assert int(restored[10])==138 and int(restored[11])==80,restored
                assert int(restored[14])==81001 and int(restored[15])==81002,restored
                cursor=reader.cursor()
                status,_=request(port,"POST","/command",
                    {**base,"operation":"DESCEND","request":81001})
                assert status==202,status
                reader.wait("ERROR",cursor,lambda f:f[4]=="PLANET_SHUTTLE_DENIED")
                with reader.condition:
                    until=time.monotonic()+8
                    while not reader.finished and time.monotonic()<until:
                        reader.condition.wait(.1)
                assert reader.finished and not reader.error,reader.error
            finally:reader.close()
        finally:
            proc.terminate()
            try:proc.communicate(timeout=4)
            except subprocess.TimeoutExpired:proc.kill();proc.communicate()
        native=subprocess.run([str(fixture),"--verify",str(db),str(body),
            str(latitude),str(longitude),str(pilot),str(initial_mass),str(ship),str(passenger)],
            capture_output=True,text=True,timeout=30,check=True)
        assert "PASS native committed shuttle" in native.stdout,native.stdout
        print("PASS PLAY-13D.4C.5 real native host/relay shuttle delivery, return, and SQLite restart")

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--fixture",type=Path,required=True)
    parser.add_argument("--host",type=Path,required=True)
    parser.add_argument("--relay",type=Path,required=True)
    args=parser.parse_args()
    main(args.fixture.resolve(),args.host.resolve(),args.relay.resolve())
