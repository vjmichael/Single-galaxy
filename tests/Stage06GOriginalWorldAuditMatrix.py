#!/usr/bin/env python3
"""S6-G: integrated ORIGINAL SQLite reconciliation across the five person
roles introduced by Stage 6. Direct native subprocess test only: does NOT
prove packaged Windows/SGDX Job Object/cgroup or Godot human play.
"""
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time


def command(argv, timeout=180):
    return subprocess.run(list(map(str, argv)), capture_output=True,
                          text=True, timeout=timeout)


def stage(fixture, operation, path):
    result=command([fixture,operation,path])
    assert result.returncode==0, result.stderr[:2500]
    fields=result.stdout.strip().split("|")
    assert len(fields)==11 and fields[0]=="TRANSFER",result.stdout
    return tuple(map(int,fields[1:]))


def audit(host, save, expected_ok=True):
    result=command([host,save,"--stage6-audit"],timeout=45)
    if not expected_ok:
        assert result.returncode!=0 and not result.stdout.strip(), (
            "second original-world reader bypassed writer lease",
            result.returncode,result.stdout,result.stderr)
        return None
    assert result.returncode==0,result.stderr[:2500]
    fields=result.stdout.strip().split("|")
    assert len(fields)==17 and fields[:2]==["GFRAME","1"],result.stdout
    values=tuple(map(int,fields[2:]))
    # tick, generation, state_hash, ore-equivalent, actual kg, credits,
    # body genesis count, finite region count, physical count, sites,
    # shuttles, player people, ecology_v130, native_surface_v133, air_v135
    assert values[1]>0 and values[2]>0 and values[3]>=0,values
    return values


def transfer(host, save):
    proc=subprocess.Popen([str(host),str(save),"--planet-transfer"],
                          stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True, bufsize=1)
    output=queue.Queue()

    def reader():
        for line in proc.stdout:
            output.put(line.rstrip("\n"))
        output.put(None)

    thread=threading.Thread(target=reader,daemon=True)
    thread.start()
    deadline=time.monotonic()+30
    while time.monotonic()<deadline:
        try:
            line=output.get(timeout=0.5)
        except queue.Empty:
            continue
        assert line is not None,proc.stderr.read()[:2500]
        assert line.startswith(("TSITE|","TFRAME|")),line
        if line.startswith("TFRAME|"):
            assert len(line.split("|"))==19
            return proc,thread
    proc.kill()
    raise AssertionError("no original shuttle simulation frame")


def main():
    if len(sys.argv)!=3:
        raise SystemExit("Usage: matrix.py STAGE6_FIXTURE PLAYER_HOST")
    fixture=Path(sys.argv[1]).resolve()
    host=Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="sg-stage06g-original-") as temp:
        base=Path(temp)
        for label,option,person in [
                ("ORBIT","--seed-orbit",0),
                ("BOARDED","--seed-boarded",5),
                ("TRANSIT","--seed-transit",6),
                ("LANDED_SHUTTLE","--seed-shuttle",5),
                ("SURFACE","--seed-surface",7)]:
            save=base/(label.lower()+".sqlite")
            before=stage(fixture,option,save)
            assert before[0]==person,(label,before)
            first=audit(host,save)
            assert first[0]==before[2],(label,first,before)
            assert first[1]==before[3],(label,first,before)
            assert first[2]==before[9],(label,first,before)
            assert first[3]==before[8],(label,first,before)
            assert first[9]>=1 and first[10]>=1 and first[11]==1,(label,first)
            after=stage(fixture,"--inspect-transfer",save)
            assert after==before,("read-only audit modified source",label,before,after)
            second=audit(host,save)
            assert second==first,("audit alone changed canonical state",label)
            if option=="--seed-boarded":
                proc,reader=transfer(host,save)
                audit(host,save,False)
                proc.stdin.write("QUIT\n")
                proc.stdin.flush()
                proc.wait(timeout=20)
                reader.join(timeout=4)
                assert proc.returncode==0,proc.stderr.read()[:2000]
                committed=stage(fixture,"--inspect-transfer",save)
                updated=audit(host,save)
                assert updated[0]==committed[2] and updated[1]==committed[3]
        # Missing database must never be synthesized by read-only admission.
        absent=base/"unborn.sqlite"
        audit(host,absent,False)
        assert not absent.exists()
        print("S6-G original person/shuttle/site/finite-stock audit matrix PASS")


if __name__=="__main__":
    main()
