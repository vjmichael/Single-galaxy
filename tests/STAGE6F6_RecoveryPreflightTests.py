#!/usr/bin/env python3
"""S6-F6 source-owned installed recovery role/one-writer acceptance.
Uses only authentic SQLite worlds constructed with real native commands. No
compiler or host execution has been performed merely by authoring this test.
"""
import pathlib
import queue
import subprocess
import sys
import tempfile
import threading
import time


def inspect(fixture, flag, save):
    run = subprocess.run([str(fixture), flag, str(save)],
                         capture_output=True, text=True, timeout=180)
    assert run.returncode==0, run.stderr
    parts=run.stdout.strip().split("|")
    assert len(parts)==11 and parts[0]=="TRANSFER",run.stdout
    return tuple(map(int,parts[1:]))


def probe(host, save, role=None):
    run=subprocess.run([str(host),str(save),"--planet-recover"],
                       capture_output=True,text=True,timeout=20)
    if role is None:
        assert run.returncode!=0 and not run.stdout.strip(), \
            ("unsafe unexpected native recovery",run.stdout,run.stderr)
        return None
    assert run.returncode==0,run.stderr
    fields=run.stdout.strip().split("|")
    assert len(fields)==12 and fields[:2]==["RFRAME","1"],run.stdout
    numbers=list(map(int,fields[2:]))
    assert numbers[0]==role,(role,fields)
    assert numbers[-1]>0 and numbers[-2]>=0,fields
    return numbers


def start_transfer(host,save):
    p=subprocess.Popen([str(host),str(save),"--planet-transfer"],
                       stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE,text=True,bufsize=1)
    lines=queue.Queue()

    def read():
        for line in p.stdout:
            lines.put(line.strip())
        lines.put(None)

    thread=threading.Thread(target=read,daemon=True)
    thread.start()
    end=time.monotonic()+20
    while time.monotonic()<end:
        try:
            line=lines.get(timeout=0.5)
        except queue.Empty:
            continue
        assert line is not None,"native transfer died before original TFRAME"
        if line.startswith("TFRAME|"):
            return p,thread,line,lines
        assert line.startswith("TSITE|"),line
    raise AssertionError("no source-authoritative transfer frame")


def main():
    fixture=pathlib.Path(sys.argv[1]).resolve()
    host=pathlib.Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="sg-s6f6-reconcile-") as temp:
        root=pathlib.Path(temp)
        fixtures=[
            ("--seed-orbit",2,0,0),
            ("--seed-boarded",3,5,0),
            ("--seed-transit",3,6,1),
            ("--seed-shuttle",3,5,2),
            ("--seed",4,7,2),
        ]
        saves={}
        for index,(flag,role,state,phase) in enumerate(fixtures):
            save=root/("original-%d.sqlite"%index)
            before=inspect(fixture,flag,save)
            # saved player tuple: state, phase, tick, gen, ship, body,
            # descent receipt, ascent receipt, mass and state hash
            assert before[0]==state and before[1]==phase,(flag,before)
            r=probe(host,save,role)
            assert r[1]==state and r[7]==phase,(flag,r)
            assert r[8]==before[2] and r[9]==before[3],(flag,r,before)
            assert r[2]==before[4],(flag,r,before)
            after=inspect(fixture,"--inspect-transfer",save)
            assert after==before,("read-only probe changed committed world",flag)
            saves[flag]=save

        # A preflight must NEVER open a second original writer, even a
        # supposedly read-only one, while an installed host owns the OS lease.
        live=saves["--seed-boarded"]
        p,thread,initial,updates=start_transfer(host,live)
        assert initial.split("|")[5]=="5"
        probe(host,live,None)
        # Native 100ms clock must progress autonomously for at least 16
        # committed original steps with NO Godot or client command at all.
        initial_tick=int(initial.split("|")[10])
        deadline=time.monotonic()+30
        later=None
        while time.monotonic()<deadline:
            try:
                event=updates.get(timeout=0.5)
            except queue.Empty:
                continue
            assert event is not None,"native clock stopped before sustained ticks"
            packet=event.split("|")
            if packet[0]=="TFRAME" and packet[3]=="0" and                     int(packet[10])>=initial_tick+16:
                later=packet
                break
        assert later is not None,"no 16-tick independent native writer progression"
        assert int(later[11])>int(initial.split("|")[11])
        p.kill()  # the original writer is deliberately not given QUIT
        p.wait(timeout=20)
        thread.join(timeout=4)
        fresh=inspect(fixture,"--inspect-transfer",live)
        assert fresh[2]>=int(later[10]) and fresh[3]>=int(later[11])
        time.sleep(0.3)  # closed app has no clock; no imaginary elapsed time
        assert inspect(fixture,"--inspect-transfer",live)==fresh
        result=probe(host,live,3)
        assert result[8]==fresh[2] and result[9]==fresh[3]

        # A killed native process must NOT trigger wall-clock catch-up. The
        # saved in-flight world carries exactly the original residual ticks.
        in_flight=saves["--seed-transit"]
        saved=inspect(fixture,"--inspect-transfer",in_flight)
        time.sleep(0.4)  # deliberate offline time: NO native process running
        still=inspect(fixture,"--inspect-transfer",in_flight)
        assert still==saved
        reopened=probe(host,in_flight,3)
        assert reopened[1]==6 and reopened[7]==1
        assert reopened[8]==saved[2] and reopened[9]==saved[3]
        # A new authorized native child resumes only FROM persisted tick.
        p,thread,first,_updates=start_transfer(host,in_flight)
        fields=first.split("|")
        assert int(fields[5])==6 and int(fields[9])==1
        assert int(fields[10])==saved[2] and int(fields[11])==saved[3]
        p.stdin.write("QUIT\n")
        p.stdin.flush()
        p.wait(timeout=20)
        thread.join(timeout=4)
        assert p.returncode==0,p.stderr.read()[:2000]

        # Missing files are never manufactured by a source probe.
        absent=root/"no-original-save.sqlite"
        probe(host,absent,None)
        assert not absent.exists()
        print("S6-F6 original recovery roles, writer lease and offline tick safety PASS")


if __name__=="__main__":
    main()
