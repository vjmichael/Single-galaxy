#!/usr/bin/env python3
"""SGDX-01 installed-host exception smoke: no client frames, private incident."""
import json
import pathlib
import subprocess
import sys
import tempfile


def check(label, binary):
    with tempfile.TemporaryDirectory(prefix="sgdx_host_") as directory:
        save = pathlib.Path(directory) / "world.sqlite"
        # Both hosts reject 10 ms as a runtime error inside the guarded main.
        # The failure must leave a private report, never a fake SGVIEW frame.
        result = subprocess.run(
            [binary, str(save), "10", "5"],
            capture_output=True, text=True, timeout=15, check=False
        )
        if result.returncode != 1 or result.stdout:
            raise AssertionError(
                f"{label} exit={result.returncode} stdout={result.stdout[:150]!r}"
            )
        incident_dir = pathlib.Path(str(save) + ".diagnostics")
        reports = list(incident_dir.glob("incident-*.json"))
        if len(reports) != 1:
            raise AssertionError(f"{label}: expected one private report; got {len(reports)}")
        record = json.loads(reports[0].read_text(encoding="utf-8"))
        required_role = "colony_live_host" if label == "colony" else "interactive_host"
        if (record.get("schema") != "SGDX-01"
                or record.get("kind") != "unhandled_exception"
                or record.get("host") != required_role
                or record.get("phase") != "startup"
                or record.get("last_committed_tick") != 0
                or record.get("last_world_generation") != 0
                or not isinstance(record.get("epoch_ms"), int)):
            raise AssertionError(f"{label}: invalid incident context {record!r}")
        if label == "colony":
            if "invalid run limits" not in record.get("message", ""):
                raise AssertionError(f"{label}: exception evidence missing")
        elif "period must be" not in record.get("message", ""):
            raise AssertionError(f"{label}: exception evidence missing")
        if save.exists() or str(save) in reports[0].read_text(encoding="utf-8"):
            raise AssertionError(f"{label}: failed startup created save or disclosed save path")
        if sys.platform != "win32":
            if incident_dir.stat().st_mode & 0o077:
                raise AssertionError(f"{label}: incident directory readable by other users")
            if reports[0].stat().st_mode & 0o077:
                raise AssertionError(f"{label}: incident readable by other users")
        print(f"PASS SGDX-01 {label}: private incident with startup context, no SGVIEW leak")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: SGDX01HostExceptionTests.py COLONY_HOST INTERACTIVE_HOST")
    check("colony", sys.argv[1])
    check("interactive", sys.argv[2])
