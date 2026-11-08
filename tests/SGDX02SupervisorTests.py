#!/usr/bin/env python3
"""SGDX-02 native process supervisor OS death/timeout and custody tests."""
import json
import os
import pathlib
import signal
import subprocess
import sys
import tempfile
import time


def check(supervisor, child):
    with tempfile.TemporaryDirectory(prefix="sgdx02_") as tmp:
        root = pathlib.Path(tmp)
        save = root / "world.sqlite"

        def run(mode, *, timeout_ms=3000, record_clean=False, allow_core=False):
            args = [supervisor, "--save", str(save), "--role", "game_host",
                    "--build-sha", "f" * 40, "--timeout-ms", str(timeout_ms)]
            if record_clean:
                args.append("--record-clean")
            if allow_core:
                args.append("--allow-core")
            args += ["--", child, mode]
            result = subprocess.run(args, capture_output=True, text=True,
                                    timeout=12, check=False)
            directory = pathlib.Path(str(save) + ".diagnostics")
            records = sorted(directory.glob("process-*.json")) if directory.exists() else []
            return result, json.loads(records[-1].read_text()) if records else None, records

        clean, item, _ = run("clean")
        assert clean.returncode == 0 and clean.stdout == "SGVIEW|1|unmodified\n"
        assert item is None, "clean process should not produce an incident"

        failed, item, _ = run("error")
        assert failed.returncode == 7 and item["exit_code"] == 7
        assert item["reason"] == "nonzero_exit"
        assert item["build_sha"] == "f" * 40
        if os.name != "nt":
            assert item["last_committed_tick"] == 18
            assert item["last_world_generation"] == 20
            assert item["breadcrumb_count"] == 2
            assert "expected failure" in item["stderr_tail"]
        assert str(save) not in json.dumps(item)
        assert failed.stdout == ""

        crashed, item, _ = run("segv")
        if os.name != "nt" and item["reason"] == "signal":
            assert crashed.returncode == 128 + signal.SIGSEGV
            assert item["signal"] == signal.SIGSEGV
        else:
            # An instrumented child may catch SIGSEGV itself and exit 1.
            assert crashed.returncode != 0 and item["reason"] == "nonzero_exit"
            if os.name != "nt":
                assert ("UndefinedBehaviorSanitizer" in item["stderr_tail"]
                        or "AddressSanitizer" in item["stderr_tail"])
        assert item["core_opt_in"] is False

        aborted, item, _ = run("abort")
        assert aborted.returncode != 0
        if os.name != "nt":
            assert item["last_committed_tick"] == 18

        timeout, item, _ = run("hang", timeout_ms=200)
        assert timeout.returncode == 124 and item["reason"] == "timeout"
        assert item["elapsed_ms"] < 7000

        large, item, _ = run("stderr")
        assert large.returncode == 21
        assert len(item["stderr_tail"]) <= 8192
        assert "TAIL-MARKER" in item["stderr_tail"]

        binary, item, _ = run("binary")
        assert binary.returncode == 23
        assert item["reason"] == "nonzero_exit"
        assert "binary-tail:" in item["stderr_tail"]
        assert chr(0xff) in item["stderr_tail"] and chr(0) in item["stderr_tail"]

        ok, item, _ = run("clean", record_clean=True)
        assert ok.returncode == 0 and item["reason"] == "clean_exit"

        if os.name != "nt":
            # Process-tree timeout must reap the group, not just the parent.
            survivor, item, _ = run("descendant", timeout_ms=220)
            assert survivor.returncode == 124 and item["reason"] == "timeout"
            pid = int(survivor.stdout.strip().split("=")[1])
            time.sleep(0.15)
            try:
                os.kill(pid, 0)
            except ProcessLookupError:
                pass
            else:
                # A zombie reparented to PID 1 is not an executing worker.
                stat = pathlib.Path(f"/proc/{pid}/stat")
                if stat.exists() and stat.read_text().split()[2] != "Z":
                    raise AssertionError("descendant remained active after timeout")

            directory = pathlib.Path(str(save) + ".diagnostics")
            assert directory.stat().st_mode & 0o077 == 0
            for path in directory.glob("process-*.json"):
                assert path.stat().st_mode & 0o077 == 0
            # Repeated incidents must not fill disk indefinitely.
            for _ in range(18):
                r, _, _ = run("error")
                assert r.returncode == 7
            assert len(list(directory.glob("process-*.json"))) == 16
            assert not list(directory.glob("*.tmp"))

            other = root / "other"
            other.mkdir()
            guarded = pathlib.Path(str(root / "other-save.sqlite") + ".diagnostics")
            guarded.symlink_to(other, target_is_directory=True)
            attack = subprocess.run([supervisor, "--save", str(root / "other-save.sqlite"),
                                     "--role", "game_host", "--", child, "error"],
                                    capture_output=True, timeout=10)
            assert attack.returncode != 0
            assert not list(other.glob("*.json")), "must not follow diagnostic directory symlink"

        print("PASS SGDX-02 OS signal/timeout, private committed-tick breadcrumbs,"
              " bounded stderr, process tree, rotation, clean stdout, symlink refusal")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: SGDX02SupervisorTests.py SUPERVISOR FAULT_CHILD")
    check(sys.argv[1], sys.argv[2])
