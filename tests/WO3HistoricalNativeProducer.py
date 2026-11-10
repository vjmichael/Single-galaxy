#!/usr/bin/env python3
"""Attest a reproduced historical *v85 saved SQLite* from the authentic v86-era
game kernel. This is NOT an archived v86 database or an old signed executable.

The pinned original PLAY13D test already issues real native commands to an
original PersistentWorld. We instrument ONLY that historical test's one v85
checkpoint to copy its actual SQLite journal before later operations change
its snapshot version. The historical game kernel, persistence library, build
and API sources remain byte-for-byte as committed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

SOURCE_COMMIT = "d2cf352346a0574225e156f89e1baf1ac8ed9bd9"
SOURCE_TEST_BLOB = "23516163160b4e8730553371eb04666082ae6231"
SOURCE_TEST = "tests/PLAY13D_RegionalFacilitiesTests.cpp"
CAPTURE_ENV = "SG_WO3_HISTORICAL_CAPTURE_OUTPUT"


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for b in iter(lambda: f.read(1024 * 1024), b""):
            h.update(b)
    return h.hexdigest()


def git(source, *args):
    return subprocess.check_output(
        ["git", "-C", str(source), *args], text=True
    ).strip()


def validate_checkout(source):
    if git(source, "rev-parse", "HEAD") != SOURCE_COMMIT:
        raise RuntimeError("historical producer Git head is not pinned")
    if git(source, "hash-object", SOURCE_TEST) != SOURCE_TEST_BLOB:
        raise RuntimeError("historical source test differs from actual Git blob")


def prepare(source):
    validate_checkout(source)
    if git(source, "status", "--porcelain"):
        raise RuntimeError("source must be clean before adding capture")
    path = source / SOURCE_TEST
    original = path.read_text(encoding="utf-8")
    anchor = '        CHECK(w.world().canonical_state().rfind("galaxy-kernel-v85|",0)==0);'
    if original.count(anchor) != 1:
        raise RuntimeError("real native v85 checkpoint anchor missing/ambiguous")
    source_injection = (
        anchor + "\n"
        '        if(const char* target=std::getenv("' + CAPTURE_ENV + '")) {\n'
        '            PersistentWorld::recover_copy(db.string(),target);\n'
        '            return; // preserve original v85 journal before later operations\n'
        '        }'
    )
    original = original.replace(anchor, source_injection)
    original = original.replace("#include <chrono>\n", "#include <chrono>\n#include <cstdlib>\n", 1)
    main_anchor = '        scenario(dir/"farm.sqlite",true);'
    if original.count(main_anchor) != 1:
        raise RuntimeError("native farm scenario anchor missing/ambiguous")
    original = original.replace(
        main_anchor,
        main_anchor + "\n"
        '        if(std::getenv("' + CAPTURE_ENV + '")) {\n'
        '            fs::remove_all(dir);\n'
        '            return 0; // original native v85 checkpoint captured\n'
        '        }'
    )
    path.write_text(original, encoding="utf-8")
    changed = git(source, "diff", "--name-only").splitlines()
    if changed != [SOURCE_TEST]:
        raise RuntimeError("instrumentation attempted to modify original kernel")
    print("Original v86-era game source pinned to", SOURCE_COMMIT)
    print("Historical native core unmodified; observational test only:", SOURCE_TEST)


def attest(source, binary, world, output, env_file):
    if git(source, "rev-parse", "HEAD") != SOURCE_COMMIT:
        raise RuntimeError("producer source head changed")
    if git(source, "diff", "--name-only").splitlines() != [SOURCE_TEST]:
        raise RuntimeError("unexpected historical producer source modification")
    if not binary.is_file() or not world.is_file():
        raise RuntimeError("historical native binary or original journal missing")
    with world.open("rb") as f:
        if f.read(16) != b"SQLite format 3\x00":
            raise RuntimeError("historical writer did not produce native SQLite")
    attestation = {
        "schema": "SG-WO3-HISTORICAL-NATIVE-V85-REPRODUCTION-01",
        "producer_source_commit": SOURCE_COMMIT,
        "original_test_git_blob": SOURCE_TEST_BLOB,
        "instrumentation": "v85 SQLite recover_copy of source's original native journal",
        "archived_artifact": False,
        "sqlite_sha256": sha256(world),
        "producer_executable_sha256": sha256(binary),
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(attestation, indent=2) + "\n")
    lines = [
        "SG_WO3_V85_ORIGINAL_SQLITE=" + str(world.resolve()),
        "SG_WO3_V85_ORIGINAL_SHA256=" + attestation["sqlite_sha256"],
        "SG_WO3_V85_SOURCE_BINARY=" + str(binary.resolve()),
        "SG_WO3_V85_SOURCE_BINARY_SHA256=" + attestation["producer_executable_sha256"],
        "SG_WO3_V85_SOURCE_COMMIT=" + SOURCE_COMMIT,
        "SG_WO3_V85_ATTESTATION=" + str(output.resolve()),
    ]
    with env_file.open("a", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("Historical native source reproduction attested, not archived.")
    print(json.dumps(attestation, indent=2))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("mode", choices=["prepare", "attest"])
    p.add_argument("source", type=Path)
    p.add_argument("--binary", type=Path)
    p.add_argument("--sqlite", type=Path)
    p.add_argument("--manifest", type=Path)
    p.add_argument("--github-env", type=Path)
    a = p.parse_args()
    if a.mode == "prepare":
        prepare(a.source)
    else:
        if not all((a.binary, a.sqlite, a.manifest, a.github_env)):
            p.error("attest needs --binary --sqlite --manifest --github-env")
        attest(a.source, a.binary, a.sqlite, a.manifest, a.github_env)


if __name__ == "__main__":
    main()
