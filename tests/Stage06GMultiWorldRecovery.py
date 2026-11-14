#!/usr/bin/env python3
"""S6-GR: concurrent real original SQLite planetary hosts and crash recovery.

Four independent world files are accepted in parallel, each completing 256+
authoritative ticks before an actual guardian SIGKILL, single-writer lock
exclusion, offline tick freeze, durable restart and stale-session refusal.
This is NOT a claim of cross-world networking or physical installed gameplay.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

COUNT=4
TICKS=256


def main(args):
    assert len(args.sha)==40 and all(c in "0123456789abcdef" for c in args.sha)
    with tempfile.TemporaryDirectory(prefix="s6g-four-original-worlds-") as td:
        root=Path(td)
        processes=[]
        opened=[]
        started=time.monotonic()
        try:
            for i in range(COUNT):
                folder=root/f"cell-{i}"
                folder.mkdir()
                packet=folder/"packet.json"
                log=(folder/"run.log").open("w",encoding="utf-8")
                opened.append(log)
                cmd=[sys.executable,str(args.single),"--supervisor",str(args.supervisor),
                     "--host",str(args.host),"--fixture",str(args.fixture),
                     "--sha",args.sha,"--ticks",str(TICKS),"--packet",str(packet)]
                proc=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT,
                                      stdin=subprocess.DEVNULL)
                processes.append((i,proc,folder))
            for i,proc,folder in processes:
                try:exit_code=proc.wait(timeout=150)
                except subprocess.TimeoutExpired:
                    proc.kill();proc.wait(timeout=5)
                    raise RuntimeError(f"original cell {i} exceeded bounded 150s")
                if exit_code:
                    raise RuntimeError(f"original cell {i} failed with exit {exit_code}: "+
                                       (folder/"run.log").read_text()[-8000:])
            for log in opened:log.close()
            opened=[]
            packets=[]
            for i,proc,folder in processes:
                out=(folder/"run.log").read_text(encoding="utf-8")
                assert "guardian death" in out and "stale session DENY PASS" in out,(
                    i,out[-5000:])
                data=json.loads((folder/"packet.json").read_text())
                assert data["schema"]=="SG-S6G-ORIGINAL-TERRAIN-01"
                assert data["source_sha"]==args.sha
                atlas=Path(data["atlas_path"])
                assert atlas.is_file() and hashlib.sha256(atlas.read_bytes()).hexdigest()==data["atlas_sha256"]
                assert len(json.loads(atlas.read_text())["tiles"])==9
                packets.append(data)
            assert len({p["atlas_path"] for p in packets})==COUNT
            assert len({p["pose"]["session"] for p in packets})==COUNT,(
                "worlds must not reuse one session identity")
            evidence={"schema":"SG-S6GR-FOUR-ORIGINAL-WORLDS-01",
                      "exact_source_head":args.sha,"status":"native_multiworld_recovery_passed_only",
                      "original_worlds":COUNT,"required_ticks_each":TICKS,
                      "min_aggregate_committed_ticks":COUNT*TICKS,
                      "guardian_killed_each":True,"restarted_after_crash_each":True,
                      "single_writer_denied_each":True,"offline_tick_freeze_each":True,
                      "stale_session_denied_each":True,
                      "elapsed_seconds":round(time.monotonic()-started,3),
                      "packet_sha256":[p["atlas_sha256"] for p in packets],
                      "limitations":["independent worlds, not a production network or intercell transport",
                                     "not physical Windows SCM or systemd delegated cgroup",
                                     "not installed gameplay or Radeon acceptance"]}
            if args.output:
                dest=Path(args.output).resolve()
                dest.parent.mkdir(parents=True,exist_ok=True)
                temp=dest.with_suffix(dest.suffix+".tmp")
                temp.write_text(json.dumps(evidence,indent=2,sort_keys=True)+"\n",encoding="utf-8")
                os.replace(temp,dest)
            print("S6-GR PASS four simultaneously supervised original SQLite worlds, "
                  "256+ committed ticks each, guardian death, writer fencing, "
                  "restart, stale-session denial; source="+args.sha)
        finally:
            for _,proc,_ in processes:
                if proc.poll() is None:
                    proc.kill();proc.wait(timeout=10)
            for log in opened:log.close()


if __name__=="__main__":
    ap=argparse.ArgumentParser()
    for name in ("single","supervisor","host","fixture","sha"):
        ap.add_argument("--"+name,required=True)
    ap.add_argument("--output")
    main(ap.parse_args())
