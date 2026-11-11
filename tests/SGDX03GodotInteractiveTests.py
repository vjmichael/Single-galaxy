#!/usr/bin/env python3
"""SGDX-03: REAL OS supervisor, full-duplex game pipes, death, SQLite and dumps.
Usage: SCRIPT SUPERVISOR FAULT_CHILD NATIVE_PLAYER_HOST FINITE_ROCK_SEED
This is native Windows/Linux acceptance, not a stubbed Godot frame.
"""
import ctypes
from contextlib import closing
import json
import queue
import threading
import os
from pathlib import Path
import signal
import sqlite3
import subprocess
import sys
import tempfile
import time

supervisor, fault, native, seed = map(Path, sys.argv[1:])
assert all(x.is_file() for x in (supervisor, fault, native, seed))

def command(save, executable, *args, windows_dump=False, interactive=True):
    argv = [str(supervisor), "--save", str(save), "--role", "godot_ship",
            "--record-clean"]
    if interactive:
        argv.append("--interactive")
    else:
        argv.extend(["--timeout-ms", "450"])
    if windows_dump and os.name == "nt":
        argv.append("--windows-minidump")
    return argv + ["--", str(executable), *map(str, args)]

def receipts(save):
    directory = Path(str(save) + ".diagnostics")
    rows = sorted(directory.glob("process-*.json"))
    assert rows, f"Missing real SGDX sidecar under {directory}"
    return [json.loads(p.read_text(encoding="utf-8")) for p in rows]

def bounded_line(process, phase, timeout_seconds=15):
    """Bound every native pipe read, including missing startup/SGDX receipts.

    A crashed or blocked supervisor can keep stdout open indefinitely. A
    direct readline() hangs CTest without useful evidence; Windows pipe
    handles are not selectable, so use a daemon reader and a deadline on
    both operating systems. Never invent a receipt on timeout.
    """
    inbox = queue.Queue(maxsize=1)
    def read():
        try:
            inbox.put((True, process.stdout.readline()))
        except BaseException as error:
            inbox.put((False, error))
    threading.Thread(target=read, daemon=True).start()
    try:
        ok, line = inbox.get(timeout=timeout_seconds)
    except queue.Empty:
        process.kill()
        try:
            process.wait(timeout=4)
        except subprocess.TimeoutExpired:
            pass
        raise AssertionError(
            f"SGDX03 native pipe timed out at {phase}; "
            f"guardian PID={process.pid} exit={process.poll()}") from None
    if not ok:
        raise AssertionError(f"SGDX03 native pipe failed at {phase}: {line!r}")
    if not line:
        raise AssertionError(
            f"SGDX03 native pipe closed before {phase}; "
            f"guardian PID={process.pid} exit={process.poll()}")
    return line.strip()

def process_dead(pid):
    if os.name == "nt":
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
        kernel.OpenProcess.restype = ctypes.c_void_p
        kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        kernel.WaitForSingleObject.restype = ctypes.c_ulong
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = kernel.OpenProcess(0x00100000, False, pid)  # SYNCHRONIZE
        if not handle:
            return True
        try:
            return kernel.WaitForSingleObject(handle, 0) == 0
        finally:
            kernel.CloseHandle(handle)
    try:
        os.kill(pid, 0)
        stat = Path(f"/proc/{pid}/stat")
        return stat.exists() and stat.read_text().split()[2] == "Z"
    except ProcessLookupError:
        return True

def await_dead(pid):
    for _ in range(70):
        if process_dead(pid):
            return
        time.sleep(0.05)
    raise AssertionError(f"Orphaned supervised native child PID {pid}")

def forced_kill(pid):
    if os.name != "nt":
        os.kill(pid, signal.SIGKILL)
        return
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
    kernel.OpenProcess.restype = ctypes.c_void_p
    kernel.TerminateProcess.argtypes = [ctypes.c_void_p, ctypes.c_uint]
    kernel.TerminateProcess.restype = ctypes.c_int
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel.OpenProcess(0x0001, False, pid) # PROCESS_TERMINATE
    assert handle, f"OpenProcess failed on child {pid}"
    try:
        assert kernel.TerminateProcess(handle, 73)
    finally:
        kernel.CloseHandle(handle)

def start_fault(save):
    child = subprocess.Popen(command(save, fault, "pipe"),
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True, bufsize=1)
    hello = bounded_line(child, "fault READY")
    assert hello.startswith("READY|"), hello
    return child, int(hello.split("|")[1])

def check_faults(root):
    clean_save = root / "pipe-clean.sqlite"
    result = subprocess.run(command(clean_save, fault, "pipe"),
                            input="POSE|7|45\nQUIT\n", capture_output=True,
                            text=True, timeout=20)
    assert result.returncode == 0 and result.stdout.splitlines()[1:] == [
        "ECHO|POSE|7|45", "BYE|CLEAN"], result
    assert receipts(clean_save)[-1]["reason"] == "clean_exit"
    assert not clean_save.exists(), "SGDX must NEVER create authoritative SQLite"

    forced = root / "pipe-forced.sqlite"
    p, pid = start_fault(forced)
    p.stdin.write("MOVE|7|1\n");p.stdin.flush()
    assert bounded_line(p, "forced ECHO") == "ECHO|MOVE|7|1"
    forced_kill(pid)
    out, err = p.communicate(timeout=18)
    assert p.returncode != 0 and "ECHO" not in out, (p.returncode,out,err)
    await_dead(pid)
    item = receipts(forced)[-1]
    assert item["role"] == "godot_ship" and item["reason"] in (
        "signal", "nonzero_exit", "unhandled_exception"), item

    crash = root / "pipe-segv.sqlite"
    result = subprocess.run(command(crash, fault, "pipe", windows_dump=True),
                            input="CRASH\n", capture_output=True, text=True, timeout=30)
    assert result.returncode != 0 and result.stdout.startswith("READY|"), result
    incident = receipts(crash)[-1]
    assert incident["reason"] in ("signal","unhandled_exception"), incident
    if os.name == "nt":
        assert incident["reason"] == "unhandled_exception", incident
        assert incident["detail"] == "crash-time minidump recorded", incident
        dumps = list(Path(str(crash) + ".diagnostics").glob("crash-*.dmp"))
        assert len(dumps) == 1 and dumps[0].read_bytes()[:4] == b"MDMP", dumps
        assert 32 <= dumps[0].stat().st_size <= 64*1024*1024
        from SGDX02SupervisorTests import assert_windows_private_acl
        assert_windows_private_acl(dumps[0])
    else:
        assert incident["signal"] == signal.SIGSEGV, incident  # native ServerProcessDiagnostics JSON key

    timed = root / "pipe-timeout.sqlite"
    result = subprocess.run(command(timed, fault, "pipe", interactive=False),
                            input="HANG\n",capture_output=True,text=True,timeout=20)
    assert result.returncode == 124 and result.stdout.startswith("READY|"), result
    assert receipts(timed)[-1]["reason"] == "timeout"

    orphan = root / "pipe-parent-forced.sqlite"
    p, pid = start_fault(orphan)
    p.kill() # Native job-on-close / Linux parent-death signal, not client QUIT.
    p.communicate(timeout=12)
    await_dead(pid)
    assert not orphan.exists()

def send(p, payload, needs_rock=False):
    time.sleep(0.075)
    p.stdin.write(payload + "\n");p.stdin.flush()
    first = bounded_line(p, "native ACK/SHIP").split("|")
    assert first and first[0] in ("ACK","DENY","SHIP"),first
    rock = None
    if needs_rock and first[0] == "SHIP":
        rock = bounded_line(p, "native ROCK").split("|")
        assert rock[0] in ("ROCK","ROCKNONE"),rock
        nav = bounded_line(p, "same-tick native station NAV").split("|")
        assert len(nav)==11 and nav[:5]==["NAV","1",first[2],first[3],first[6]],nav
        assert nav[5:7]==first[7:9] and int(nav[7])>0,nav
        assert sum(int(v)**2 for v in nav[8:11])<=1000000000**2,nav
    return first,rock

def start_original(save):
    p = subprocess.Popen(command(save,native,save),stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                         text=True,bufsize=1)
    pose=bounded_line(p, "original POSE").split("|")
    ship=bounded_line(p, "original SHIP").split("|")
    rock=bounded_line(p, "original initial ROCK").split("|")
    nav=bounded_line(p, "original initial NAV").split("|")
    assert len(nav)==11 and nav[:5]==["NAV","1",pose[2],"0",pose[10]],nav
    assert nav[5:7]==ship[7:9] and int(nav[7])>0,nav
    assert pose[:2] == ["POSE","1"] and ship[:2] == ["SHIP","1"] and rock[0] in (
        "ROCK","ROCKNONE"),(pose,ship,rock)
    assert ship[2] == pose[2] == rock[2]
    return p,pose,ship,rock

def check_original_world(root):
    save = root / "actual-world.sqlite"
    genesis = subprocess.run([str(seed),str(save)],capture_output=True,
                             text=True,timeout=25)
    assert genesis.returncode == 0 and genesis.stdout.startswith("04A-FIXTURE|"),genesis
    _,station,ship,rock,stock = genesis.stdout.strip().split("|")
    p,pose,shipframe,frame=start_original(save)
    token=pose[2]
    seat,_=send(p,f"SEAT|{token}|1|0|1")
    assert seat[0] == "ACK" and seat[11:] == ["1","1"],seat
    undock,contact=send(p,f"FLIGHT|{token}|2|1|UND",True)
    assert undock[0] == "SHIP" and undock[5] == "0" and undock[16] == "0",undock
    assert contact[0] == "ROCK" and contact[7] == rock and contact[8] == stock,contact
    # The *same* supervised duplex channel must support repeated authentic
    # commands with real C++ CTest state, not just a simple echo fixture.
    motion,contact=send(p,f"FLIGHT|{token}|3|1|FWD",True)
    assert motion[0] == "SHIP" and motion[5] == "0" and contact[7] == rock,(motion,contact)
    p.stdin.write("QUIT\n");p.stdin.flush()
    assert p.wait(timeout=15) == 0,p.stderr.read()
    done=receipts(save)[-1]
    assert done["reason"] == "clean_exit" and done["role"] == "godot_ship",done
    if os.name != "nt":
        assert done["breadcrumb_count"] >= 2 and done["last_committed_tick"] >= 1,done
    with closing(sqlite3.connect(save)) as con:
        assert con.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
    # The game reopens its real persisted world through a fresh *supervisor*.
    p2,pose2,ship2,rock2=start_original(save)
    assert pose2[2] != token and ship2[16] == "0", (pose2,ship2)
    assert rock2[0] == "ROCK" and rock2[7] == rock and rock2[8] == stock, rock2
    p2.stdin.write("QUIT\n");p2.stdin.flush()
    assert p2.wait(timeout=15) == 0,p2.stderr.read()
    assert receipts(save)[-1]["reason"] == "clean_exit"

    # Abrupt guardian loss against the ACTUAL original game. SGDX Windows
    # Job Object and Linux parent-death custody must relinquish the SQLite
    # writer before a new supervised owner can open the same saved world.
    p3, pose3, vessel3, rock3 = start_original(save)
    assert vessel3[16] == "0" and rock3[7] == rock and rock3[8] == stock
    p3.kill()
    p3.communicate(timeout=12)
    resumed = subprocess.run(command(save, native, save),
                             input="QUIT\n",capture_output=True,text=True,timeout=15)
    assert resumed.returncode == 0, (resumed.returncode,resumed.stderr[:600])
    records = resumed.stdout.splitlines()
    assert len(records) >= 4 and records[0].startswith("POSE|1|") and (
        "ROCK|1|" in records[2]), records[:4]
    nav=records[3].split("|")
    assert len(nav)==11 and nav[:5]==["NAV","1",records[0].split("|")[2],"0",records[1].split("|")[6]],nav
    assert nav[5:7]==records[1].split("|")[7:9],nav
    restored = records[2].split("|")
    assert restored[7] == rock and restored[8] == stock
    with closing(sqlite3.connect(save)) as con:
        assert con.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
    assert receipts(save)[-1]["reason"] == "clean_exit"

with tempfile.TemporaryDirectory(prefix="sgdx03_godot_") as tmp:
    root=Path(tmp)
    check_faults(root)
    check_original_world(root)
print("SGDX-03 real Windows/Linux PASS: bidirectional original game pipe, supervised crash and child kill, watchdog, parent force kill, original SQLite restart, Windows MDMP ACL")
