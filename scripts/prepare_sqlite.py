#!/usr/bin/env python3
"""Acquire official pinned SQLite amalgamation and verify original archive SHA3-256."""
import argparse
import hashlib
import io
from pathlib import Path
import urllib.request
import zipfile

VERSION = "3540100"
URL = f"https://www.sqlite.org/2026/sqlite-amalgamation-{VERSION}.zip"
SHA3 = "92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30"
ROOT = Path(__file__).resolve().parents[1] / "vendor" / "sqlite"

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--zip", type=Path, help="Use manually downloaded official archive offline")
    args = parser.parse_args()
    if args.zip:
        payload = args.zip.read_bytes()
    else:
        with urllib.request.urlopen(URL, timeout=45) as response:
            payload = response.read(10 * 1024 * 1024 + 1)
    if len(payload) > 10 * 1024 * 1024:
        raise SystemExit("Archive exceeds permitted size")
    digest = hashlib.sha3_256(payload).hexdigest()
    if digest != SHA3:
        raise SystemExit(f"SQLite archive SHA3-256 mismatch. Actual: {digest}")
    with zipfile.ZipFile(io.BytesIO(payload)) as z:
        for name in ("sqlite3.c", "sqlite3.h"):
            archived = f"sqlite-amalgamation-{VERSION}/{name}"
            if z.getinfo(archived).file_size > 15 * 1024 * 1024:
                raise SystemExit("Unexpected source file size")
            data = z.read(archived)
            ROOT.mkdir(parents=True, exist_ok=True)
            (ROOT / name).write_bytes(data)
    print("Verified SQLite 3.54.1 amalgamation extracted successfully")

if __name__ == "__main__":
    main()
