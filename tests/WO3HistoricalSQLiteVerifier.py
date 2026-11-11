#!/usr/bin/env python3
"""WO3-F: fail-closed authentic historical native-source SQLite verification.

The original pre-v109 kernel at its compiler-repaired November 10 source wrote real *v85* SQLite state, and its v86
legal-residence upgrade retained ship homes in memory. It did NOT write a
v86 SQLite state with invented planetary legal addresses. This gate accepts
only a pinned, unchanged vintage *kernel* and its real saved command journal,
instrumented observation-only at the original v85 checkpoint. Reproduction
is identified as a reproduction, not misrepresented as an archived artifact.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import sqlite3
import subprocess
import sys

SOURCE_COMMIT = "23bad9be38936d61b8c29b5be5f3900bec87fa6d"
SOURCE_BLOB = "6add9dfbf4cbb0a19794abaadd7e699239923e08"
SCHEMA = "SG-WO3-HISTORICAL-NATIVE-V85-REPRODUCTION-01"


def fail(message):
    raise RuntimeError("WO3-F original producer provenance required: " + message)


def verify(path, digest, label):
    p = Path(path)
    if not p.is_file():
        fail(label + ": original file missing")
    if not re.fullmatch(r"[0-9a-fA-F]{64}", digest or ""):
        fail(label + ": exact SHA-256 attestation missing")
    h = hashlib.sha256()
    with p.open("rb") as handle:
        for data in iter(lambda: handle.read(1024 * 1024), b""):
            h.update(data)
    if h.hexdigest() != digest.lower():
        fail(label + ": native source/executable/SQLite SHA mismatch")
    return p


def main():
    if len(sys.argv) != 2:
        fail("native historical-import acceptance executable missing")
    commit = os.environ.get("SG_WO3_V85_SOURCE_COMMIT", "")
    if commit != SOURCE_COMMIT:
        fail("historical native source commit is unpinned or wrong")
    world_sha = os.environ.get("SG_WO3_V85_ORIGINAL_SHA256", "")
    binary_sha = os.environ.get("SG_WO3_V85_SOURCE_BINARY_SHA256", "")
    world = verify(os.environ.get("SG_WO3_V85_ORIGINAL_SQLITE", ""),
                   world_sha, "historical native v85 SQLite")
    binary = verify(os.environ.get("SG_WO3_V85_SOURCE_BINARY", ""),
                    binary_sha, "historical native producer executable")
    manifest_path = Path(os.environ.get("SG_WO3_V85_ATTESTATION", ""))
    if not manifest_path.is_file():
        fail("independent native producer manifest missing")
    manifest = json.loads(manifest_path.read_text())
    required = {
        "schema": SCHEMA,
        "producer_source_commit": SOURCE_COMMIT,
        "original_test_git_blob": SOURCE_BLOB,
        "archived_artifact": False,
        "sqlite_sha256": world_sha.lower(),
        "producer_executable_sha256": binary_sha.lower(),
    }
    for key, expected in required.items():
        if manifest.get(key) != expected:
            fail("producer manifest provenance mismatch: " + key)
    uri = world.resolve().as_uri() + "?mode=ro&immutable=1"
    with sqlite3.connect(uri, uri=True) as db:
        if db.execute("PRAGMA integrity_check").fetchone() != ("ok",):
            fail("original saved SQLite integrity check failed")
        for table in ("operation_log", "snapshots", "receipts", "world_meta"):
            if not db.execute(
                "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?",
                (table,),
            ).fetchone():
                fail("original saved SQLite missing native table " + table)
        genesis = db.execute(
            "SELECT kind,tick FROM operation_log WHERE generation=1"
        ).fetchone()
        if genesis != (0, 0):
            fail("original journal lacks tick-zero native genesis")
        if db.execute(
            "SELECT COUNT(*) FROM operation_log WHERE kind=1"
        ).fetchone()[0] < 1:
            fail("historical producer did not execute native commands")
        if db.execute(
            "SELECT COUNT(*) FROM receipts"
        ).fetchone()[0] < 1:
            fail("historical producer lacks native accepted-command receipts")
        latest = db.execute(
            "SELECT state FROM snapshots ORDER BY generation DESC LIMIT 1"
        ).fetchone()
        if latest is None or not bytes(latest[0]).startswith(b"galaxy-kernel-v85|"):
            fail("historical saved world is not real native v85 producer data")
    print("SOURCE-PINNED HISTORICAL REPRODUCTION; NOT AN ARCHIVED DATABASE",
          commit, "SQLite SHA256", world_sha)
    subprocess.run([sys.argv[1], str(world)], check=True)
    # Fail closed if native acceptance wrote through or corrupted the source.
    verify(world, world_sha, "original SQLite after independent native test")
    verify(binary, binary_sha, "producer executable after independent native test")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, sqlite3.Error, json.JSONDecodeError,
            subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
