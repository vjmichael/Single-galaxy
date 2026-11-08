#!/usr/bin/env python3
"""SGDX-02 actual game host and native cell-worker smoke (requires full kernel)."""
import json
import pathlib
import subprocess
import sys
import tempfile

BUILD_SHA = "f" * 40


def supervise(supervisor, save, role, binary, arguments, *, record_clean=False):
    command = [supervisor, "--save", str(save), "--role", role,
               "--build-sha", BUILD_SHA, "--timeout-ms", "60000"]
    if record_clean:
        command.append("--record-clean")
    command += ["--", binary, *arguments]
    result = subprocess.run(command, capture_output=True, text=True,
                            timeout=75, check=False)
    directory = pathlib.Path(str(save) + ".diagnostics")
    process = sorted(directory.glob("process-*.json")) if directory.exists() else []
    exception = sorted(directory.glob("incident-*.json")) if directory.exists() else []
    return result, [json.loads(x.read_text()) for x in process], [
        json.loads(x.read_text()) for x in exception]


def check(supervisor, colony, interactive, worker):
    with tempfile.TemporaryDirectory(prefix="sgdx02_host_") as directory:
        root = pathlib.Path(directory)
        # Both game hosts must preserve their SGDX-01 exception evidence
        # while SGDX-02 independently captures the child process exit.
        for name, exe in (("colony_live_host", colony), ("interactive_host", interactive)):
            save = root / (name + ".sqlite")
            result, process, exception = supervise(
                supervisor, save, name, exe, [str(save), "10", "5"])
            if result.returncode != 1 or result.stdout:
                raise AssertionError(f"{name}: unexpected startup failure {result!r}")
            if len(process) != 1 or len(exception) != 1:
                raise AssertionError(f"{name}: missing dual diagnostics receipts")
            if (process[0]["schema"] != "SGDX-02"
                    or process[0]["build_sha"] != BUILD_SHA
                    or process[0]["reason"] != "nonzero_exit"
                    or exception[0]["schema"] != "SGDX-01"):
                raise AssertionError(f"{name}: incorrect independent receipts")
            if save.exists():
                raise AssertionError(f"{name}: invalid start created a game save")

        # A real native process worker has no world writer. Its refused
        # invocation still yields an original diagnostic process receipt.
        worker_save = root / "worker.sqlite"
        result, process, _ = supervise(
            supervisor, worker_save, "cell_worker", worker, ["--invalid"])
        if result.returncode != 64 or len(process) != 1:
            raise AssertionError("original native worker rejection was not recorded")
        if worker_save.exists() or process[0]["last_committed_tick"] != 0:
            raise AssertionError("native worker invented a committed world state")

        # Positive world path: child stdout remains actual SGVIEW data, and
        # SGDX-02 observes the original SQLite commit ticks independently.
        for name, exe in (("colony_live_host", colony), ("interactive_host", interactive)):
            save = root / (name + "-positive.sqlite")
            result, process, exception = supervise(
                supervisor, save, name, exe, [str(save), "20", "5"],
                record_clean=True)
            if result.returncode != 0:
                raise AssertionError(
                    f"{name}: genuine host failed: {result.returncode} {result.stderr[:400]}")
            if (not save.exists() or "SGVIEW|" not in result.stdout
                    or len(process) != 1 or exception):
                raise AssertionError(f"{name}: missing native persistence/output evidence")
            if (process[0]["reason"] != "clean_exit"
                    or process[0]["last_committed_tick"] < 5
                    or process[0]["breadcrumb_count"] < 5):
                raise AssertionError(f"{name}: missing genuine committed-tick breadcrumbs")
        print("PASS SGDX-02 two native game hosts and real cell worker evidence")


if __name__ == "__main__":
    if len(sys.argv) != 5:
        raise SystemExit("usage: SGDX02GameHostTests.py SUPERVISOR COLONY INTERACTIVE CELL_WORKER")
    check(*sys.argv[1:])
