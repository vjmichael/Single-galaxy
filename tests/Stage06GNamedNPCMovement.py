#!/usr/bin/env python3
"""S6-F5-D original installed-host NPC protocol and SQLite recovery matrix.
Subprocess coverage does NOT substitute for physical Godot/Windows testing.
"""
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time


def run(argv, timeout=180):
    result=subprocess.run(list(map(str,argv)),capture_output=True,
                          text=True,timeout=timeout)
    assert result.returncode==0, (argv,result.stderr[-5000:])
    return result.stdout.strip()


def launch(host, save):
    proc=subprocess.Popen([str(host),str(save),"--planet"],
        stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
        text=True,bufsize=1)
    pending=queue.Queue()
    def reader():
        for line in proc.stdout:
            pending.put(line.strip())
        pending.put(None)
    thread=threading.Thread(target=reader,daemon=True)
    thread.start()
    return proc,pending,thread


def await_frame(pending, marker, timeout=35):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        try:
            line=pending.get(timeout=min(0.5,max(0.01,end-time.monotonic())))
        except queue.Empty:
            continue
        assert line is not None, "original host exited before its native receipt"
        fields=line.split("|")
        assert fields[0] in ("PFRAME","NROSTER","NACK","DENY"),line
        if fields[0]=="PFRAME":
            assert len(fields)==40,line
        if fields[0]=="NROSTER":
            assert len(fields)>=5 and len(fields)==5+int(fields[4]),line
        if fields[0]=="NACK":
            assert len(fields)==10,line
        if line.startswith(marker):
            return fields
    raise AssertionError("Original NPC host timed out waiting for "+marker)


def snapshot(fixture, save):
    fields=run([fixture,"--inspect-npc",save]).split("|")
    assert fields[0]=="NPCSTATE" and len(fields)==5+int(fields[4])*4,fields
    residents={}
    for k in range(int(fields[4])):
        i=5+k*4
        residents[int(fields[i])]=(int(fields[i+1]),int(fields[i+2]),
                                   int(fields[i+3]))
    return int(fields[1]),int(fields[2]),int(fields[3]),residents


def main():
    if len(sys.argv)!=3:
        raise SystemExit("usage: named-npc-matrix FIXTURE ORIGINAL_PLAYER_HOST")
    fixture=Path(sys.argv[1]).resolve()
    host=Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="sg-stage6-npc-original-") as td:
        save=Path(td)/"original_land.sqlite"
        run([fixture,"--seed-surface",save])
        before=snapshot(fixture,save)
        assert before[3],"a genuine original surface NPC must be present"
        proc,pending,thread=launch(host,save)
        try:
            p=await_frame(pending,"PFRAME|1|")
            roster=await_frame(pending,"NROSTER|1|")
            session=int(p[2])
            assert int(roster[2])==session and int(roster[4])>0,roster
            npc=int(roster[5])
            assert npc in before[3],roster
            # The next action is refused when a client invents its epoch.
            proc.stdin.write(f"NMOVE|{session+1}|1|{npc}|200|0\n")
            proc.stdin.flush()
            await_frame(pending,"DENY|1|1|planet-session-mismatch")
            # Wait for one autonomous ORIGINAL SQLite tick.
            while True:
                row=await_frame(pending,"PFRAME|1|")
                if int(row[15])>int(p[15]):break
            proc.stdin.write(f"NMOVE|{session}|2|{npc}|200|0\n")
            proc.stdin.flush()
            ack=await_frame(pending,"NACK|1|")
            assert int(ack[2])==session and int(ack[3])==2
            assert int(ack[4])==npc and int(ack[9])>0
            assert int(ack[8])>before[1]
        finally:
            if proc.poll() is None:
                proc.stdin.write("QUIT\n")
                proc.stdin.flush()
            proc.wait(timeout=20)
            thread.join(timeout=2)
        assert proc.returncode==0,proc.stderr.read()[-5000:]
        after=snapshot(fixture,save)
        assert after[1]>=int(ack[8])
        assert after[3][npc]==(int(ack[5]),int(ack[6]),int(ack[9]))
        assert after[3][npc][0]==before[3][npc][0]+200
        assert after[3][npc][1]==before[3][npc][1]
        assert after[2]!=before[2],"original NPC motion did not persist"
        # New writer reopens the same original journal without inventing NPCs.
        proc2,pending2,thread2=launch(host,save)
        try:
            restarted=await_frame(pending2,"PFRAME|1|")
            assert int(restarted[2])!=session
            new_roster=await_frame(pending2,"NROSTER|1|")
            assert npc in [int(x) for x in new_roster[5:]]
            proc2.stdin.write(f"NMOVE|{session}|3|{npc}|200|0\n")
            proc2.stdin.flush()
            await_frame(pending2,"DENY|1|3|planet-session-mismatch")
        finally:
            if proc2.poll() is None:
                proc2.stdin.write("QUIT\n")
                proc2.stdin.flush()
            proc2.wait(timeout=20)
            thread2.join(timeout=2)
        assert proc2.returncode==0,proc2.stderr.read()[-5000:]
        print("S6-F5-D original named NPC native host motion, replay, SQLite restart PASS")


if __name__=="__main__":
    main()
