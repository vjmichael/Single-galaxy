#!/usr/bin/env python3
"""S6-F5-F: ORIGINAL orbit -> boarded shuttle -> real descent -> surface ->
explicit hatch reboard -> real ascent -> original ship. One SQLite world.
The direct native subprocess fixture is NOT SGDX OS-tree nor installed-Godot
acceptance. Tests must be run on the current exact source head before claiming
success.
"""
import pathlib
import queue
import subprocess
import sys
import tempfile
import threading
import time


def fixture(exe, op, save):
    result = subprocess.run([str(exe), op, str(save)],
                            capture_output=True, text=True, timeout=180)
    if result.returncode:
        raise AssertionError("fixture failed: " + result.stderr[:3000])
    f = result.stdout.strip().split("|")
    assert len(f) == 11 and f[0] == "TRANSFER", result.stdout
    return dict(state=int(f[1]), phase=int(f[2]), tick=int(f[3]),
                gen=int(f[4]), ship=int(f[5]), body=int(f[6]),
                descent=int(f[7]), ascent=int(f[8]),
                mass=int(f[9]), hash=int(f[10]))


def launch(host, save, mode):
    process = subprocess.Popen([str(host), str(save), mode],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True, bufsize=1)
    output = queue.Queue()

    def pump():
        for line in process.stdout:
            output.put(line.strip())
        output.put(None)

    reader = threading.Thread(target=pump, daemon=True)
    reader.start()
    return process, output, reader


def receive(q, predicate, deadline_seconds=45):
    end = time.monotonic() + deadline_seconds
    while time.monotonic() < end:
        try:
            line = q.get(timeout=max(0.001, min(0.5, end-time.monotonic())))
        except queue.Empty:
            continue
        assert line is not None, "original native host disconnected before expected receipt"
        f = line.split("|")
        assert f[0] in ("TSITE", "TFRAME", "TLAND", "TRETURN",
                        "PFRAME", "PLEAVE", "DENY", "NROSTER"), line
        if f[0] == "NROSTER":
            assert len(f) >= 5 and f[1] == "1", line
            assert all(value.isdigit() for value in f[2:]), line
            count = int(f[4])
            assert 0 <= count <= 16 and len(f) == 5 + count, line
        if f[0] == "TFRAME":
            assert len(f) == 19 and f[1] == "1", line
        if f[0] == "TSITE":
            assert len(f) == 9 and f[1] == "1", line
        if f[0] == "PFRAME":
            assert len(f) == 40 and f[1] == "1", line
        if predicate(f):
            return f
    raise AssertionError("native receipt timed out after %.1fs" % deadline_seconds)


def command(proc, fields):
    proc.stdin.write("|".join(map(str, fields)) + "\n")
    proc.stdin.flush()


def stop(proc, reader, forced=False, natural=False):
    if forced:
        proc.kill()
    elif not natural and proc.poll() is None:
        proc.stdin.write("QUIT\n")
        proc.stdin.flush()
    proc.wait(timeout=20)
    reader.join(timeout=4)
    if not forced and proc.returncode:
        raise AssertionError("native process exited %s: %s" %
                             (proc.returncode, proc.stderr.read()[:2500]))


def transfer(host, save):
    proc, output, reader = launch(host, save, "--planet-transfer")
    first = receive(output, lambda f:f[0]=="TFRAME")
    assert int(first[3]) == 0 and int(first[5]) in (0,5,6), first
    assert int(first[2])>0 and int(first[12])>0, first
    return proc, output, reader, first


def wait_state(q, sess, state, phase):
    return receive(q, lambda f:f[0]=="TFRAME" and int(f[2])==sess and
                   int(f[3])==0 and int(f[5])==state and int(f[9])==phase)


def main():
    native_fixture = pathlib.Path(sys.argv[1]).resolve()
    host = pathlib.Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="sg-s6f5f-travel-") as tmp:
        save = pathlib.Path(tmp) / "real-planet-transit.sqlite"
        start = fixture(native_fixture, "--seed-orbit", save)
        assert start["state"]==0 and start["phase"]==0 and start["body"]==0
        proc, output, reader, first = transfer(host, save)
        session = int(first[2])
        assert int(first[5])==0 and int(first[9])==0
        command(proc, ["TDO", session, 1, "BOARD"])
        boarded = receive(output, lambda f:f[0]=="TFRAME" and f[3]=="1")
        assert int(boarded[5])==5 and int(boarded[9])==0
        assert int(boarded[16])>0
        command(proc, ["TDO",session,2,"DESCEND"])
        descending = receive(output,lambda f:f[0]=="TFRAME" and f[3]=="2")
        assert int(descending[5])==6 and int(descending[9])==1
        arrived = wait_state(output, session, 5, 2)
        assert int(arrived[14])>0, arrived
        # Native process death must leave one SQLite owner and a recoverable
        # real landed shuttle, not a free ghost player.
        stop(proc, reader, forced=True)
        preserved = fixture(native_fixture,"--inspect-transfer",save)
        assert preserved["state"]==5 and preserved["phase"]==2
        assert preserved["descent"]>0
        proc, output, reader, resumed = transfer(host, save)
        recovered_session = int(resumed[2])
        assert recovered_session!=session
        assert int(resumed[5])==5 and int(resumed[9])==2
        assert int(resumed[10])==preserved["tick"]
        assert int(resumed[11])==preserved["gen"]
        # A prior process's action cannot be replayed into the new owner.
        command(proc, ["TDO",session,3,"EXIT"])
        denied=receive(output,lambda f:f[0]=="DENY" and f[2]=="3")
        assert denied[3]=="stale-session"
        command(proc, ["TDO",recovered_session,1,"DISEMBARK"])
        landed=receive(output,lambda f:f[0]=="TLAND")
        assert int(landed[2])==recovered_session and int(landed[3])==1
        assert int(landed[8])>0
        stop(proc,reader,natural=True)

        on_surface=fixture(native_fixture,"--inspect-transfer",save)
        assert on_surface["state"]==7 and on_surface["descent"]>0
        planet, planet_output, planet_reader = launch(host,save,"--planet")
        pframe=receive(planet_output,lambda f:f[0]=="PFRAME" and f[3]=="0")
        assert int(pframe[5])==int(landed[5])
        psession=int(pframe[2])
        command(planet,["PREBOARD",psession,1,"REBOARD"])
        reboard=receive(planet_output,lambda f:f[0]=="PLEAVE")
        assert int(reboard[2])==psession and int(reboard[3])==1
        assert int(reboard[10])>0
        stop(planet,planet_reader,natural=True)
        shore_exit=fixture(native_fixture,"--inspect-transfer",save)
        assert shore_exit["state"]==5 and shore_exit["phase"]==2

        proc, output, reader, resumed = transfer(host,save)
        ascent_session=int(resumed[2])
        assert int(resumed[5])==5 and int(resumed[9])==2
        command(proc,["TDO",ascent_session,1,"ASCEND"])
        ascent=receive(output,lambda f:f[0]=="TFRAME" and f[3]=="1")
        assert int(ascent[5])==6 and int(ascent[9])==3
        back=wait_state(output,ascent_session,5,0)
        assert int(back[15])>0
        command(proc,["TDO",ascent_session,2,"EXIT"])
        returned=receive(output,lambda f:f[0]=="TRETURN")
        assert int(returned[2])==ascent_session and int(returned[3])==2
        assert int(returned[4])==start["ship"]
        stop(proc,reader,natural=True)
        finish=fixture(native_fixture,"--inspect-transfer",save)
        assert finish["state"]==0 and finish["phase"]==0
        assert finish["body"]==0
        assert finish["mass"]==start["mass"]  # no material creation/teleport
        assert finish["hash"]!=start["hash"]
        print("S6-F5-F native original shuttle/landing/reboard/ascent/SQLite PASS")


if __name__=="__main__":
    main()
