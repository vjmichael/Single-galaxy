#!/usr/bin/env python3
"""WO3 historical source gate: verify real immutable binary/SQLite provenance.
No source fixture is generated or patched here. Missing archive is a FAILED
test, not a skipped, fabricated, or freshly initialized historical database.
"""
import hashlib
import os
from pathlib import Path
import re
import sqlite3
import subprocess
import sys


def fail(reason):
    raise RuntimeError("WO3-F authentic source required: " + reason)


def verify(path, digest, label):
    p = Path(path)
    if not p.is_file():
        fail(label + " original file missing")
    if not re.fullmatch(r"[0-9a-fA-F]{64}", digest or ""):
        fail(label + " independently recorded SHA-256 required")
    h = hashlib.sha256()
    with p.open("rb") as handle:
        for data in iter(lambda: handle.read(1024 * 1024), b""):
            h.update(data)
    if h.hexdigest().lower() != digest.lower():
        fail(label + " SHA-256 differs from archived source")
    return p


def main():
    if len(sys.argv) != 2:
        fail("native historical-import test executable argument missing")
    original = os.environ.get("SG_WO3_V86_ORIGINAL_SQLITE", "")
    original_sha = os.environ.get("SG_WO3_V86_ORIGINAL_SHA256", "")
    binary = os.environ.get("SG_WO3_V86_SOURCE_BINARY", "")
    binary_sha = os.environ.get("SG_WO3_V86_SOURCE_BINARY_SHA256", "")
    source_commit = os.environ.get("SG_WO3_V86_SOURCE_COMMIT", "")
    if not re.fullmatch(r"[0-9a-fA-F]{40}", source_commit):
        fail("original v86 binary's exact historical Git commit required")
    world = verify(original, original_sha, "immutable archived v86 SQLite")
    verify(binary, binary_sha, "historical v86 native executable")
    # Read-only, immutable SQLite check, never create or mutate an archive.
    # The native PersistentWorld::recover_copy will independently verify
    # its full original journal, snapshots, authority, and receipt hashes.
    uri = world.resolve().as_uri() + "?mode=ro&immutable=1"
    with sqlite3.connect(uri, uri=True) as db:
        integrity = db.execute("PRAGMA integrity_check").fetchone()
        if integrity != ("ok",):
            fail("archived SQLite integrity check did not pass")
        for name in ("operation_log", "snapshots", "receipts", "world_meta"):
            if not db.execute(
                "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?",
                (name,),
            ).fetchone():
                fail("missing original table " + name)
        genesis = db.execute(
            "SELECT kind,tick FROM operation_log WHERE generation=1"
        ).fetchone()
        if genesis != (0, 0):
            fail("original journal has no valid tick-zero genesis")
        if db.execute(
            "SELECT COUNT(*) FROM operation_log WHERE kind=1"
        ).fetchone()[0] < 1:
            fail("source contains no original accepted gameplay commands")
        latest = db.execute(
            "SELECT state FROM snapshots ORDER BY generation DESC LIMIT 1"
        ).fetchone()
        if latest is None or not bytes(latest[0]).startswith(b"galaxy-kernel-v86|"):
            fail("source is not an actual v86 SQLite saved world")
    print("Authentic v86 SQLite verified; historical source", source_commit,
          "SQLite SHA-256", original_sha.lower())
    subprocess.run([sys.argv[1], str(world)], check=True)
    # Recovery must never alter either immutable historical input. Detect
    # accidental journal checkpointing, binary replacement or write-through.
    verify(original, original_sha, "immutable archived v86 SQLite after native test")
    verify(binary, binary_sha, "historical v86 executable after native test")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, sqlite3.Error, subprocess.CalledProcessError) as exc:
        print(str(exc), file=sys.stderr)
        sys.exit(1)
