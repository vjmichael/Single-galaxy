#!/usr/bin/env python3
"""PLAY-13C.2C: real colony host receives a body ID and durably changes ship frame."""
import pathlib
import sqlite3
import subprocess
import sys
import tempfile

BODY_ID=(1 << 32) | 1

def launch(executable, world, payload=b"", ticks=8):
    run=subprocess.run(
        [str(executable), str(world), "20", str(ticks)],
        input=payload, capture_output=True, timeout=45, check=False)
    if run.returncode:
        raise AssertionError(
            f"colony host returned {run.returncode}: "
            +run.stderr.decode("utf-8", "replace")[-3000:])
    return run

def snapshot(world):
    with sqlite3.connect(world) as db:
        row=db.execute(
            "SELECT state FROM snapshots ORDER BY generation DESC LIMIT 1"
        ).fetchone()
        assert row and row[0], "missing authoritative native snapshot"
        value=row[0]
        return value.decode("utf-8") if isinstance(value,bytes) else value

def main():
    executable=pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="sg_play13c_") as tmp:
        db=pathlib.Path(tmp)/"world.sqlite"
        first=launch(executable,db,f"FRAME_TRAVEL|{BODY_ID}\n".encode("ascii"),8)
        status=first.stderr.decode("utf-8","replace")
        assert "FRAME_TRANSIT_STARTED|" in status,status[-2500:]
        assert "FRAME_TRANSIT_ARRIVED|" in status,status[-2500:]
        state=snapshot(db)
        assert state.startswith("galaxy-kernel-v73|"),state[:100]
        assert f"localregion|{BODY_ID}|" in state
        assert state.count("localanchor|")==2
        assert "frametransit_pending|" not in state
        assert "frametransit_done|" in state
        assert f"shipframe|1|1|{BODY_ID}|" in state
        assert "entity|1|1|0|7|0|0|" in state, "original ship did not arrive at local origin"
        launch(executable,db,b"",5)
        state_after=snapshot(db)
        assert f"shipframe|1|1|{BODY_ID}|" in state_after
        assert state_after.count("frametransit_done|")==1
        assert "frametransit_pending|" not in state_after
    print("PASS PLAY13C2C real native host travel and restart recovery")

if __name__=="__main__":
    main()
