#!/usr/bin/env python3
"""S6-GK independent REAL cgroup-v2 admission/containment proof.

Without --delegated-root and distinct game UID/GID, check only fail-closed
admission. With explicit --delegated-root, exercise kernel cgroup.procs and
cgroup.kill, independent session detachment, migration refusal and cleanup.
No cgroup creation at the system level, systemd delegation or sudo is done here.
The caller must already be the trusted supervisor in a systemd-owned delegated
unit, with its cgroup path matching /proc/self/cgroup. This test cannot prove
systemd guardian-death behavior unless the service manager owns that guardian.
"""
import argparse
import json
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def run(args, timeout=20):
    return subprocess.run([str(x) for x in args], capture_output=True,
                          text=True, timeout=timeout, errors="replace")


def dead(pid):
    stat=Path(f"/proc/{pid}/stat")
    if not stat.exists():return True
    try:return stat.read_text().split()[2] in ("Z","X")
    except FileNotFoundError:return True


def main(args):
    assert sys.platform.startswith("linux") or not args.delegated_root
    with tempfile.TemporaryDirectory(prefix="sgdx-cgroup-gate-") as td:
        path=Path(td)
        save=path/"world.sqlite"
        base=[args.supervisor,"--save",str(save),"--role","game_host",
              "--timeout-ms","1500"]
        # These are admission denials even on a hosted runner with cgroupfs
        # read-only. Never count them as positive delegated kernel custody.
        for extra in (["--delegated-cgroup-root","/sys/fs/cgroup"],
                      ["--worker-uid","12345","--worker-gid","12345"],
                      ["--delegated-cgroup-root","/sys/fs/cgroup",
                       "--worker-uid",str(os.geteuid() if hasattr(os,"geteuid") and os.geteuid()>0 else 1000),
                       "--worker-gid","12345"]):
            result=run(base+extra+["--",args.fault,"clean"])
            assert result.returncode==64, (extra,result.returncode,result.stderr)
        if not args.delegated_root:
            print("S6-GK PASS only fail-closed cgroup admission; "
                  "positive kernel delegation NOT tested")
            return
        assert args.worker_uid and args.worker_gid and os.geteuid()!=args.worker_uid
        if args.delegated_root=="auto":
            relative=Path("/proc/self/cgroup").read_text().strip().split("::",1)[1]
            assert relative!="/", "S6-GK unit is not an independently owned systemd cgroup"
            root=Path("/sys/fs/cgroup"+relative).resolve()
        else:
            root=Path(args.delegated_root)
        assert root.is_dir() and (root/"cgroup.kill").exists()
        assert root == Path("/sys/fs/cgroup"+Path("/proc/self/cgroup").read_text().strip().split("::",1)[1]).resolve(), (
            root,Path('/proc/self/cgroup').read_text())
        privileged=["--delegated-cgroup-root",str(root),
                    "--worker-uid",str(args.worker_uid),
                    "--worker-gid",str(args.worker_gid)]
        # The game child must be able to execute the real fixture but must
        # never receive the ancestor's write privilege.
        os.chmod(path,0o755)
        denied=run(base+privileged+["--",args.fault,"cgroup-escape"])
        assert denied.returncode==0 and "CGROUP_ANCESTOR_DENIED|" in denied.stdout,(
            denied.stdout,denied.stderr,denied.returncode)
        # A child capable of setsid escapes kill(-pgid), but NOT cgroup.kill.
        detached=run(base+["--timeout-ms","500"]+privileged+
                     ["--",args.fault,"escaped-descendant"],timeout=12)
        assert detached.returncode==124, (detached.returncode,detached.stderr)
        marker="DETACHED_DESCENDANT="
        assert marker in detached.stdout,detached.stdout
        pid=int(detached.stdout.split(marker,1)[1].splitlines()[0])
        until=time.monotonic()+5
        while not dead(pid) and time.monotonic()<until:time.sleep(0.05)
        assert dead(pid),f"S6-GK delegated cgroup left escaped descendant PID={pid} alive"
        assert not list(root.glob("sgdx-child-*")),"S6-GK left a kernel leaf after completion"
        assert args.source_sha and len(args.source_sha)==40 and all(
            c in "0123456789abcdef" for c in args.source_sha)
        report={"schema":"SG-S6GK-LINUX-DELEGATED-CGROUP-01",
                "source_sha":args.source_sha,"status":"native_delegated_cgroup_passed_only",
                "cgroup_root":str(root),"supervisor_uid":os.geteuid(),
                "worker_uid":args.worker_uid,"worker_gid":args.worker_gid,
                "cgroup_v2_membership_verified":True,
                "ancestor_migration_denied":True,
                "detached_process_group_descendant_terminated_by_cgroup_kill":True,
                "empty_kernel_leaf_removed":True,
                "systemd_mainpid_guardian_loss_verified":False,
                "physical_windows_scm_verified":False,
                "supervisor_sha256":hashlib.sha256(Path(args.supervisor).read_bytes()).hexdigest(),
                "fault_child_sha256":hashlib.sha256(Path(args.fault).read_bytes()).hexdigest()}
        if args.output:
            dest=Path(args.output).resolve()
            dest.parent.mkdir(parents=True,exist_ok=True)
            staging=dest.with_suffix(dest.suffix+".tmp")
            staging.write_text(json.dumps(report,indent=2,sort_keys=True)+"\n",encoding="utf-8")
            os.replace(staging,dest)
        print("S6-GK PASS: kernel cgroup membership, denied ancestor migration, "
              "detached-descendant cgroup.kill and empty leaf removal")


if __name__=="__main__":
    ap=argparse.ArgumentParser()
    ap.add_argument("supervisor")
    ap.add_argument("fault")
    ap.add_argument("--delegated-root")
    ap.add_argument("--worker-uid",type=int)
    ap.add_argument("--worker-gid",type=int)
    ap.add_argument("--source-sha")
    ap.add_argument("--output")
    main(ap.parse_args())
