#!/usr/bin/env python3
"""SGDX-02: real supervised SQLite rollback and exact-once recovery fixture.

This verifies native PROCESS containment + SQLite transaction semantics. It is
NOT a replacement for the original game's PersistentWorld journal acceptance.
"""
import json
import os
import pathlib
import signal
import sqlite3
import subprocess
import sys
import tempfile
import time


def inspect_database(db, expected_stock, expected_receipts):
    with sqlite3.connect(db, timeout=3) as con:
        assert con.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
        assert con.execute("SELECT stock FROM inventory WHERE id=1").fetchone()[0] == expected_stock
        assert con.execute("SELECT COUNT(*) FROM receipts").fetchone()[0] == expected_receipts


def attempt_script():
    return r"""
import os,signal,sqlite3,sys,time
db,mode=sys.argv[1:]
con=sqlite3.connect(db,timeout=3,isolation_level=None)
con.execute("BEGIN IMMEDIATE")
con.execute("UPDATE inventory SET stock=stock-2 WHERE id=1")
con.execute("INSERT INTO receipts VALUES('dispatch-01',2)")
print("PREPARED|"+str(os.getpid()),flush=True)
if mode=="crash":
    os.kill(os.getpid(),signal.SIGABRT)
elif mode=="hang":
    time.sleep(60)
else:
    con.execute("COMMIT")
"""


def retry_script():
    return r"""
import sqlite3,sys
db=sys.argv[1]
con=sqlite3.connect(db,timeout=3,isolation_level=None)
con.execute("BEGIN IMMEDIATE")
row=con.execute("SELECT 1 FROM receipts WHERE id='dispatch-01'").fetchone()
if row is None:
    con.execute("UPDATE inventory SET stock=stock-2 WHERE id=1")
    con.execute("INSERT INTO receipts VALUES('dispatch-01',2)")
con.execute("COMMIT")
"""


def test(supervisor):
    with tempfile.TemporaryDirectory(prefix="sgdx02_sqlite_") as folder:
        db=pathlib.Path(folder)/"original-like.sqlite"
        with sqlite3.connect(db) as con:
            con.execute("CREATE TABLE inventory(id INTEGER PRIMARY KEY, stock INTEGER NOT NULL CHECK(stock>=0))")
            con.execute("INSERT INTO inventory VALUES(1,10)")
            con.execute("CREATE TABLE receipts(id TEXT PRIMARY KEY, amount INTEGER NOT NULL)")
        prefix=[supervisor,"--save",str(db),"--role","sqlite_recovery",
                "--build-sha","a"*40,"--timeout-ms","1500","--"]
        trial=subprocess.run(prefix+[sys.executable,"-c",attempt_script(),str(db),"hang"],
                             capture_output=True,text=True,timeout=12,check=False)
        assert trial.returncode == 124, (trial.returncode,trial.stderr[:200])
        assert "PREPARED|" in trial.stdout, "transaction did not actually begin"
        inspect_database(db,10,0)
        report_dir=pathlib.Path(str(db)+".diagnostics")
        events=[json.loads(f.read_text()) for f in report_dir.glob("process-*.json")]
        assert events and events[-1]["reason"] == "timeout"
        assert events[-1]["build_sha"] == "a"*40
        crashed=subprocess.run(prefix+[sys.executable,"-c",attempt_script(),str(db),"crash"],
                               capture_output=True,text=True,timeout=12,check=False)
        assert crashed.returncode != 0 and "PREPARED|" in crashed.stdout
        inspect_database(db,10,0)
        good_prefix=[supervisor,"--save",str(db),"--role","sqlite_recovery",
                     "--timeout-ms","5000","--"]
        for _ in range(2):
            done=subprocess.run(good_prefix+[sys.executable,"-c",retry_script(),str(db)],
                                capture_output=True,text=True,timeout=12,check=False)
            assert done.returncode == 0, (done.returncode, done.stderr)
            inspect_database(db,8,1)

        if sys.platform.startswith("linux"):
            # A hard-killed guardian must not strand a write-locked child.
            reenter=pathlib.Path(folder)/"abrupt.sqlite"
            with sqlite3.connect(reenter) as con:
                con.execute("CREATE TABLE inventory(id INTEGER PRIMARY KEY,stock INTEGER)")
                con.execute("INSERT INTO inventory VALUES(1,10)")
                con.execute("CREATE TABLE receipts(id TEXT PRIMARY KEY,amount INTEGER)")
            guardian=subprocess.Popen(
                [supervisor,"--save",str(reenter),"--role","sqlite_recovery",
                 "--timeout-ms","6000","--",sys.executable,"-c",
                 attempt_script(),str(reenter),"hang"],
                stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            try:
                line=guardian.stdout.readline().strip()
                assert line.startswith("PREPARED|"), line
                child_pid=int(line.split("|",1)[1])
                guardian.kill()
                guardian.wait(timeout=5)
                deadline=time.monotonic()+3
                while time.monotonic()<deadline:
                    stat=pathlib.Path(f"/proc/{child_pid}/stat")
                    if not stat.exists() or stat.read_text().split()[2] in ("Z","X"):
                        break
                    time.sleep(0.05)
                else:
                    raise AssertionError("SQLite child survived hard guardian loss")
                inspect_database(reenter,10,0)
                # Prove write lock released; no abandoned SQLite writer.
                with sqlite3.connect(reenter,timeout=3) as con:
                    con.execute("BEGIN IMMEDIATE")
                    con.execute("ROLLBACK")
            finally:
                if guardian.poll() is None:
                    guardian.kill()
                    guardian.wait(timeout=5)
                guardian.stdout.close()
                guardian.stderr.close()
        print("PASS SGDX-02: native supervisor killed a live SQLite transaction;"
              " crash/timeout rollback, integrity and exactly-once retry")


if __name__=="__main__":
    if len(sys.argv)!=2:
        raise SystemExit("usage: SGDX02SqliteRecoveryTests.py SUPERVISOR")
    test(sys.argv[1])
