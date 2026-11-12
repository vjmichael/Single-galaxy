#!/usr/bin/env python3
"""S6-F5-E native-process/SQLite recovery. Runs a REAL seeded landed player,
REAL desktop planetary host, autonomous ticks, SIGKILL and restart.
Does not claim installed Godot/GPU or Windows SGDX tree-custody acceptance.
"""
import pathlib
import queue
import subprocess
import sys
import tempfile
import threading
import time


def invoke_fixture(fixture, option, save):
    r = subprocess.run([str(fixture), option, str(save)], capture_output=True,
                       text=True, timeout=180)
    if r.returncode:
        raise AssertionError("native fixture: " + r.stderr[:3000])
    fields = r.stdout.strip().split("|")
    assert len(fields) == 9 and fields[0] == "CHECK", r.stdout
    return tuple(int(n) for n in fields[1:])


def launch(host, save):
    proc = subprocess.Popen([str(host), str(save), "--planet"],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, bufsize=1)
    output = queue.Queue()

    def pump():
        for line in proc.stdout:
            output.put(line.strip())
        output.put(None)

    reader = threading.Thread(target=pump, daemon=True)
    reader.start()
    return proc, output, reader


def receive(output, predicate, seconds=35):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            line = output.get(timeout=min(0.5, deadline-time.monotonic()))
        except queue.Empty:
            continue
        assert line is not None, "original host closed stdout before expected frame"
        if line.startswith("PFRAME|"):
            f = line.split("|")
            assert len(f) == 40 and f[1] == "1", line
            assert all(x.lstrip("-").isdigit() for x in f[2:]), line
        elif not line.startswith("DENY|1|"):
            raise AssertionError("Unexpected native planet frame: " + line)
        if predicate(line):
            return line
    raise AssertionError("Timed out waiting for native planet evidence")


def frame(line):
    f = line.split("|")
    return {"session": int(f[2]), "req": int(f[3]),
            "ship": int(f[4]), "body": int(f[5]), "site": int(f[17]),
            "x": int(f[8]), "y": int(f[9]), "z": int(f[10]),
            "receipt": int(f[14]), "tick": int(f[15]),
            "gen": int(f[16]), "vertical_tick": int(f[26]),
            "command": int(f[27])}


def stop(proc, reader, graceful):
    if graceful:
        proc.stdin.write("QUIT\n")
        proc.stdin.flush()
        proc.stdin.close()
    else:
        proc.kill()  # deliberate native host death, NOT a normal QUIT
    proc.wait(timeout=25)
    reader.join(timeout=3)
    if graceful:
        assert proc.returncode == 0, "native host exited nonzero: " +             proc.stderr.read()[:2000]


def main():
    fixture = pathlib.Path(sys.argv[1]).resolve()
    host = pathlib.Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="sg-s6f5e-recovery-") as tmp:
        save = pathlib.Path(tmp) / "original-landed.sqlite"
        initial = invoke_fixture(fixture, "--seed", save)
        assert save.exists()
        proc, output, reader = launch(host, save)
        first = frame(receive(output, lambda s:s.startswith("PFRAME|1|")))
        assert first["req"] == 0 and first["tick"] == initial[0]
        assert first["session"] > 0 and first["body"] > 0
        later = frame(receive(output, lambda s:s.startswith("PFRAME|1|") and
                              int(s.split("|")[15]) >= first["tick"] + 3))
        assert later["session"] == first["session"]
        assert later["gen"] > first["gen"]
        assert later["vertical_tick"] >= first["vertical_tick"]
        assert later["receipt"] == first["receipt"]
        stop(proc, reader, graceful=False)
        crashed = invoke_fixture(fixture, "--inspect", save)
        assert crashed[0] >= later["tick"] and crashed[1] >= later["gen"]
        assert crashed[2] >= later["vertical_tick"]
        assert crashed[3] == initial[3]  # no client movement at all
        proc2, output2, reader2 = launch(host, save)
        resume = frame(receive(output2, lambda s:s.startswith("PFRAME|1|")))
        assert resume["session"] != first["session"]
        # No fabricated offline ticks/health deltas on initial reentry.
        assert resume["tick"] == crashed[0] and resume["gen"] == crashed[1]
        assert resume["vertical_tick"] == crashed[2]
        assert (resume["x"], resume["y"], resume["z"]) == crashed[4:7]
        assert resume["receipt"] == crashed[3]
        # An old-session command is rejected after OS-authoritative restart.
        proc2.stdin.write(f"PMOVE|{first['session']}|1|300|0\n")
        proc2.stdin.flush()
        receive(output2,lambda s:s.startswith("DENY|1|1|planet-session-mismatch"))
        # Wait for autonomous clock ownership before a new one-use command.
        receive(output2,lambda s:s.startswith("PFRAME|1|") and
                int(s.split("|")[15]) >= resume["tick"] + 2)
        proc2.stdin.write(f"PMOVE|{resume['session']}|2|300|0\n")
        proc2.stdin.flush()
        committed = frame(receive(output2,lambda s:s.startswith("PFRAME|1|")
                                 and s.split("|")[3]=="2"))
        assert committed["command"] > 0
        assert committed["receipt"] > resume["receipt"]
        assert committed["x"] == resume["x"] + 300
        assert committed["session"] == resume["session"]
        stop(proc2, reader2, graceful=True)
        final = invoke_fixture(fixture, "--inspect", save)
        assert final[0] >= committed["tick"]
        assert final[3] == committed["receipt"]
        assert final[4] == committed["x"]
        assert final[7] != initial[7]
        print("S6-F5-E original host kill/restart/epoch/SQLite/clock PASS")


if __name__ == "__main__":
    main()
