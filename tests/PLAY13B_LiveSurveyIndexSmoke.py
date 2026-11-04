#!/usr/bin/env python3
"""PLAY-13B: real colony host starts and recovers survey index across restart."""
import pathlib
import sqlite3
import subprocess
import sys
import tempfile

def run_host(executable, world):
    proc = subprocess.run(
        [str(executable), str(world), "20", "5"],
        capture_output=True, timeout=30, check=False)
    if proc.returncode != 0:
        raise AssertionError(
            f"colony host exit {proc.returncode}: "
            + proc.stderr.decode("utf-8", "replace")[-2000:])

def main():
    executable = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="sg_play13b_") as d:
        world = pathlib.Path(d) / "world.sqlite"
        index = pathlib.Path(str(world) + ".survey.sqlite")
        run_host(executable, world)
        assert world.is_file(), "native save was not committed"
        assert index.is_file(), "survey index was not created"
        with sqlite3.connect(index) as db:
            assert db.execute(
                "SELECT 1 FROM sqlite_master WHERE type='table' AND name='observations'"
            ).fetchone(), "discovery table missing"
        run_host(executable, world)
        with sqlite3.connect(index) as db:
            assert db.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
        print("PASS PLAY13B live world restart keeps owner survey index")

if __name__ == "__main__":
    main()
