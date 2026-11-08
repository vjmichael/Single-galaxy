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
            if os.name == "nt" and mode == "segv":
                args.append("--windows-minidump")
            if allow_core:
                args.append("--allow-core")
            args += ["--", child, mode]
            result = subprocess.run(args, capture_output=True, text=True,
                                    timeout=15, check=False, errors="replace")
            directory = pathlib.Path(str(save) + ".diagnostics")
            records = sorted(directory.glob("process-*.json")) if directory.exists() else []
            return result, json.loads(records[-1].read_text()) if records else None, records

        # A genuine parent-side worker refusal is NOT a child process exit.
        worker_save = root / "worker-failure.sqlite"
        event = subprocess.run([child, "worker-report", str(worker_save)],
                               capture_output=True, timeout=10, check=False)
        assert event.returncode == 0
        worker_report = list(pathlib.Path(str(worker_save)+".diagnostics").glob("process-*.json"))
        assert len(worker_report) == 1
        report = json.loads(worker_report[0].read_text(encoding="utf-8"))
        assert report["kind"] == "worker_failure"
        assert report["role"] == "native_cell_worker"
        assert report["worker_scope"] == "spatial/v2/s1,2,3"
        assert report["worker_epoch"] == 47
        assert report["last_committed_tick"] == 99
        assert report["last_world_generation"] == 103
        assert report["reason"] == "preflight_failed"
        assert report["detail"] == "original host denied native cell preflight"
        assert report["exit_code"] == -1 and report["pid"] == 0

        clean, item, _ = run("clean")
        assert clean.returncode == 0 and clean.stdout == "SGVIEW|1|unmodified\n"
        assert item is None, "clean process should not produce an incident"

        failed, item, _ = run("error")
        assert failed.returncode == 7 and item["exit_code"] == 7
        assert item["reason"] == "nonzero_exit"
        assert item["build_sha"] == "f" * 40
        if os.name == "nt":
            assert "expected failure" in failed.stderr
            assert item["stderr_tail"] == ""
        else:
            assert item["last_committed_tick"] == 18
            assert item["last_world_generation"] == 20
            assert item["breadcrumb_count"] == 2
            assert "expected failure" in item["stderr_tail"]
        assert str(save) not in json.dumps(item)
        assert failed.stdout == ""

        crashed, item, _ = run("segv")
        if os.name == "nt":
            # The direct debugger detects a second-chance unhandled exception.
            assert crashed.returncode != 0
            assert item["reason"] == "unhandled_exception"
        elif item["reason"] == "signal":
            assert crashed.returncode == 128 + signal.SIGSEGV
            assert item["signal"] == signal.SIGSEGV
        else:
            # Sanitizers can intercept SIGSEGV and exit nonzero instead.
            assert crashed.returncode != 0 and item["reason"] == "nonzero_exit"
            assert ("UndefinedBehaviorSanitizer" in item["stderr_tail"]
                    or "AddressSanitizer" in item["stderr_tail"])
        assert item["core_opt_in"] is False
        if os.name == "nt":
            assert item["reason"] == "unhandled_exception"
            assert item["detail"] == "crash-time minidump recorded"
            dumps = list((pathlib.Path(str(save)+".diagnostics")).glob("crash-*.dmp"))
            assert len(dumps) == 1, "opt-in second-chance dump missing"
            assert dumps[0].read_bytes()[:4] == b"MDMP", "not a Windows minidump"
            assert 32 <= dumps[0].stat().st_size <= 64*1024*1024

        aborted, item, _ = run("abort")
        assert aborted.returncode != 0
        if os.name != "nt":
            assert item["last_committed_tick"] == 18

        timeout, item, _ = run("hang", timeout_ms=200)
        assert timeout.returncode == 124 and item["reason"] == "timeout"
        assert item["elapsed_ms"] < 7000

        large, item, _ = run("stderr")
        assert large.returncode == 21
        if os.name == "nt":
            # Windows currently forwards the original stderr unchanged,
            # while Linux captures a bounded tail in the JSON receipt.
            assert "TAIL-MARKER" in large.stderr
            assert item["stderr_tail"] == ""
        else:
            assert len(item["stderr_tail"]) <= 8192
            assert "TAIL-MARKER" in item["stderr_tail"]

        binary, item, _ = run("binary")
        assert binary.returncode == 23
        assert item["reason"] == "nonzero_exit"
        if os.name == "nt":
            assert "binary-tail:" in binary.stderr and chr(0) in binary.stderr
            assert item["stderr_tail"] == ""
        else:
            assert "binary-tail:" in item["stderr_tail"]
            assert chr(0xff) in item["stderr_tail"] and chr(0) in item["stderr_tail"]

        ok, item, _ = run("clean", record_clean=True)
        assert ok.returncode == 0 and item["reason"] == "clean_exit"

        if os.name == "nt":
            # The Windows Job Object must kill a genuine child->grandchild
            # tree on timeout, not merely terminate the parent process.
            import ctypes
            from ctypes import wintypes
            descendant, item, _ = run("descendant", timeout_ms=2500)
            assert descendant.returncode == 124 and item["reason"] == "timeout"
            assert descendant.stdout.startswith("DESCENDANT=")
            pid = int(descendant.stdout.split("=",1)[1].strip())
            kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel.OpenProcess.argtypes = [wintypes.DWORD,wintypes.BOOL,wintypes.DWORD]
            kernel.OpenProcess.restype = wintypes.HANDLE
            kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE,wintypes.DWORD]
            kernel.WaitForSingleObject.restype = wintypes.DWORD
            kernel.CloseHandle.argtypes = [wintypes.HANDLE]
            handle = kernel.OpenProcess(0x00100000,False,pid)
            if handle:
                try:
                    if kernel.WaitForSingleObject(handle,0) != 0:
                        raise AssertionError("Job Object left real descendant running")
                finally:
                    kernel.CloseHandle(handle)

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

            # A supervisor SIGKILL must not leave its directly supervised
            # authoritative game process running without diagnostics.
            guardian = subprocess.Popen(
                [supervisor, "--save", str(save), "--role", "game_host",
                 "--timeout-ms", "5000", "--", child, "pid-hang"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                first = guardian.stdout.readline().strip()
                if not first.startswith("CHILD_PID="):
                    raise AssertionError("fixture did not start supervised process")
                target_pid = int(first.split("=", 1)[1])
                guardian.kill()
                guardian.wait(timeout=5)
                deadline = time.monotonic() + 3
                while time.monotonic() < deadline:
                    stat = pathlib.Path(f"/proc/{target_pid}/stat")
                    if not stat.exists() or stat.read_text().split()[2] in ("Z", "X"):
                        break
                    time.sleep(0.05)
                else:
                    raise AssertionError("supervised game survived guardian SIGKILL")
            finally:
                if guardian.poll() is None:
                    guardian.kill()
                    guardian.wait(timeout=5)
                guardian.stdout.close()
                guardian.stderr.close()

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
