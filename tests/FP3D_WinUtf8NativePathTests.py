#!/usr/bin/env python3
"""FP3D-WIN-UTF8: real supervised native Windows/Linux Unicode save-path test.

Runs the real original station and cockpit C++ processes under SGDX on
two NEW temporary SQLite worlds. No fake stdout, no user's existing
save, no arbitrary world reinitialization. Required because the actual
Godot userdata path contains a Unicode em dash, which Windows CRT narrow
main() can silently convert to ANSI rather than sqlite3's required UTF-8.
"""
from __future__ import annotations
import argparse
import json
import sqlite3
import subprocess
import tempfile
from pathlib import Path


def verify_native(supervisor: Path, host: Path, db: Path, role: str,
                  station: bool) -> None:
    command = [
        str(supervisor), "--save", str(db), "--role", role,
        "--interactive", "--record-clean", "--", str(host), str(db),
    ]
    if station:
        command.append("--explicit-test-station")
    outcome = subprocess.run(
        command,
        input="QUIT\n",
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=45,
        check=False,
    )
    if outcome.returncode != 0:
        raise AssertionError(
            f"{role} exited {outcome.returncode}: "
            f"stdout={outcome.stdout[-1600:]!r}, "
            f"stderr={outcome.stderr[-1600:]!r}"
        )
    first = "PERSON|2|" if station else "POSE|"
    if first not in outcome.stdout:
        raise AssertionError(
            f"{role} lacked authentic original C++ {first} frame: "
            f"{outcome.stdout[-1600:]!r}"
        )
    if not db.is_file():
        raise AssertionError(f"{role} did not create original SQLite file")
    with sqlite3.connect(db) as sqlite:
        integrity = sqlite.execute("PRAGMA integrity_check").fetchone()[0]
        if integrity != "ok":
            raise AssertionError(f"{role} persisted invalid SQLite: {integrity}")
        tables = sqlite.execute(
            "SELECT COUNT(*) FROM sqlite_master WHERE type='table'"
        ).fetchone()[0]
        if tables < 10:
            raise AssertionError(f"{role} did not create original world schema")
    directory = Path(str(db) + ".diagnostics")
    receipts = list(directory.glob("process-*.json"))
    if not receipts:
        raise AssertionError(
            f"{role} missing original private SGDX process receipt "
            "(Unicode diagnostic path failed)"
        )
    if "SGDX-02 diagnostic sidecar write failed" in outcome.stderr:
        raise AssertionError(f"{role} private Unicode SGDX receipt failed")
    observed = [json.loads(p.read_text(encoding="utf-8")) for p in receipts]
    if not any(
        r.get("role") == role and r.get("reason") == "clean_exit"
        for r in observed
    ):
        raise AssertionError(f"{role} lacked verified original clean process custody")
    print(f"{role} PASS: true Unicode filesystem, original C++ frame, SQLite integrity, private SGDX receipt")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--supervisor", required=True, type=Path)
    parser.add_argument("--station", required=True, type=Path)
    parser.add_argument("--cockpit", required=True, type=Path)
    args = parser.parse_args()
    for path in (args.supervisor, args.station, args.cockpit):
        if not path.is_file():
            raise AssertionError(f"missing genuine native binary: {path}")
    with tempfile.TemporaryDirectory(prefix="SG_colony_—_owned_world_") as tmp:
        root = Path(tmp)
        verify_native(
            args.supervisor, args.station,
            root / "Colony — Departure Dock.sqlite", "utf8_station", True,
        )
        verify_native(
            args.supervisor, args.cockpit,
            root / "White Ship — Cockpit.sqlite", "utf8_cockpit", False,
        )
    print("FP3D-WIN-UTF8 PASS: two authentic SGDX children, preserved Unicode SQLite filenames and private diagnostics")


if __name__ == "__main__":
    main()
