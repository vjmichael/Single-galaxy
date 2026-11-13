#!/usr/bin/env python3
"""S6-G native OS custody: actual planetary writer behind SGDX, journal and atlas.
This test does NOT claim physical installed-GPU or human acceptance.
"""
import argparse
import hashlib
import json
import pathlib
import queue
import subprocess
import threading
import tempfile
import time

def run(argv, timeout=180):
    out=subprocess.run([str(x) for x in argv],capture_output=True,text=True,
                       timeout=timeout,errors="replace")
    assert out.returncode==0, (argv,out.stderr[-2500:])
    return out.stdout.strip()

def original_state(fixture,save):
    data=run([fixture,"--inspect",save]).split("|")
    assert len(data)==9 and data[0]=="CHECK",data
    return tuple(map(int,data[1:]))

def probe(host,save,admit):
    r=subprocess.run([str(host),str(save),"--planet-recover"],
                     capture_output=True,text=True,timeout=20)
    if not admit:
        assert r.returncode!=0 and not r.stdout.strip(), "overlapping original writer admitted"
        return None
    assert r.returncode==0,r.stderr[-2000:]
    f=r.stdout.strip().split("|")
    assert len(f)==12 and f[:3]==["RFRAME","1","4"],f
    return f

def start(supervisor,host,save,sha):
    command=[str(supervisor),"--save",str(save),"--role","godot_planet",
             "--build-sha",sha,"--interactive","--record-clean","--",
             str(host),str(save),"--planet"]
    p=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE,text=True,bufsize=1,errors="replace")
    q=queue.Queue()
    def stream():
        try:
            for line in p.stdout:
                q.put(line.rstrip("\r\n"))
        finally:q.put(None)
    t=threading.Thread(target=stream,daemon=True)
    t.start()
    return p,q,t

def receive(q,predicate,timeout=40):
    until=time.monotonic()+timeout
    while time.monotonic()<until:
        try:line=q.get(timeout=min(0.5,max(0.001,until-time.monotonic())))
        except queue.Empty:continue
        assert line is not None,"supervised planet closed without required receipt"
        f=line.split("|")
        assert f[0] in ("PFRAME","NROSTER","DENY"),line[:240]
        if f[0]=="PFRAME":
            assert len(f)==40 and f[1]=="1",f
            assert all(x.lstrip("-").isdigit() for x in f[2:]),f
        if predicate(f):return f
    raise AssertionError("original planet timed out before required receipt")

def info(f):
    return dict(session=int(f[2]),ship=int(f[4]),body=f[5],
                x_mm=int(f[8]),z_mm=int(f[10]),receipt=int(f[14]),
                tick=int(f[15]),gen=int(f[16]),site=int(f[17]))

def finish(p,t,kill):
    if kill:p.kill()
    else:
        p.stdin.write("QUIT\n")
        p.stdin.flush()
    p.wait(timeout=18)
    t.join(timeout=6)
    assert not t.is_alive(),"guardian death left original child pipe alive"
    if not kill:assert p.returncode==0,"clean original world shutdown failed"

def main(args):
    assert len(args.sha)==40 and all(c in "0123456789abcdef" for c in args.sha)
    with tempfile.TemporaryDirectory(prefix="s6g-real-planet-") as root:
        save=pathlib.Path(root)/"original-surface.sqlite"
        assert run([args.fixture,"--seed",save]).startswith("CHECK|")
        p,q,t=start(args.supervisor,args.host,save,args.sha)
        try:
            first=info(receive(q,lambda f:f[0]=="PFRAME" and f[3]=="0"))
            assert first["session"]>0 and first["site"]>0 and first["body"]!="0"
            atlas=pathlib.Path(str(save)+".stage6.walkpatch.json")
            deadline=time.monotonic()+8
            while not atlas.is_file() and time.monotonic()<deadline:
                time.sleep(0.1)
            assert atlas.is_file(),"native process did not publish real 3x3 atlas"
            raw=atlas.read_bytes()
            packet=json.loads(raw)
            assert len(raw)<=1000000
            assert packet.get("schema")=="SG-STAGE6-WALKTILES-01"
            assert int(packet["session"])==first["session"]
            assert len(packet["tiles"])==9
            assert all(t["body"]==first["body"] and int(t["ship"])==first["ship"]
                       for t in packet["tiles"])
            probe(args.host,save,False)
            later=info(receive(q,lambda f:f[0]=="PFRAME" and
                                 int(f[15])>=first["tick"]+args.ticks,
                                 max(45,int(args.ticks/5)+45)))
            assert later["gen"]>first["gen"]
            if args.packet:
                evidence=pathlib.Path(args.packet).resolve()
                evidence.parent.mkdir(parents=True,exist_ok=True)
                copy=evidence.parent/"original-planet.stage6.walkpatch.json"
                copy.write_bytes(raw)
                evidence.write_text(json.dumps(dict(
                    schema="SG-S6G-ORIGINAL-TERRAIN-01",source_sha=args.sha,
                    pose=first,atlas_path=str(copy),
                    atlas_sha256=hashlib.sha256(raw).hexdigest()),indent=2),
                    encoding="utf-8")
            finish(p,t,True) # kill the guardian, not the source host
            recovered=None
            deadline=time.monotonic()+12
            while time.monotonic()<deadline:
                try:
                    recovered=probe(args.host,save,True)
                    break
                except AssertionError:time.sleep(0.1)
            assert recovered is not None,"guardian death retained writer lease"
            frozen=original_state(args.fixture,save)
            assert frozen[0]>=later["tick"] and frozen[1]>=later["gen"]
            time.sleep(0.45)
            assert original_state(args.fixture,save)==frozen,"offline time fabricated world tick"
            assert int(recovered[-2])==frozen[0] and int(recovered[-1])==frozen[1]
            p2,q2,t2=start(args.supervisor,args.host,save,args.sha)
            try:
                resumed=info(receive(q2,lambda f:f[0]=="PFRAME" and f[3]=="0"))
                assert resumed["session"]!=first["session"]
                assert resumed["tick"]==frozen[0] and resumed["gen"]==frozen[1]
                p2.stdin.write("PMOVE|%d|1|300|0\n"%first["session"])
                p2.stdin.flush()
                denied=receive(q2,lambda f:f[0]=="DENY" and f[2]=="1")
                assert "session-mismatch" in denied[3],denied
                finish(p2,t2,False)
            finally:
                if p2.poll() is None:p2.kill();p2.wait(timeout=10)
            print("S6-G real SGDX planet %d+ ticks, writer contention, guardian death, "
                  "offline freeze, SQLite resume and stale session DENY PASS"
                  % args.ticks)
        finally:
            if p.poll() is None:p.kill();p.wait(timeout=10)

if __name__=="__main__":
    ap=argparse.ArgumentParser()
    for name in ("supervisor","host","fixture","sha"):
        ap.add_argument("--"+name,required=True)
    ap.add_argument("--packet",default="")
    ap.add_argument("--ticks",type=int,default=16)
    args=ap.parse_args()
    if not 16<=args.ticks<=4096:
        ap.error("ticks outside 16..4096 bounded original-world acceptance")
    main(args)
