#!/usr/bin/env python3
"""S6-GK: independent OS-owned systemd MainPID death / cgroup-v2 custody.

Run only on a separately authorized systemd host with sudo systemd-run.
Creates one finite named transient service using the fixed trusted SGDX
supervisor and fixed child fixture. No Docker, generic privileged exec, game
installation, GPU test, or claim of physical Windows evidence.
"""
import argparse
import grp
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

def call(argv, timeout=25, required=True):
    p=subprocess.run(list(map(str,argv)),capture_output=True,text=True,
                     errors="replace",timeout=timeout,check=False)
    if required and p.returncode:
        raise RuntimeError(f"OS command={argv[:3]} exit={p.returncode}: "+
                           (p.stderr+" "+p.stdout)[-1600:])
    return p

def property_of(unit, prop):
    return call(["systemctl","show",unit,"--property="+prop,"--value"],required=False).stdout.strip()

def dead(pid):
    stat=Path(f"/proc/{pid}/stat")
    if not stat.exists(): return True
    try: return stat.read_text().split()[2] in ("Z","X")
    except FileNotFoundError: return True

def journal(unit):
    return call(["sudo","journalctl","-u",unit,"--no-pager","-o","cat","-n","150"],
                timeout=8,required=False).stdout

def main(args):
    sha=args.source_sha
    if not re.fullmatch(r"[0-9a-f]{40}",sha):
        raise RuntimeError("exact 40-character source SHA required")
    supervisor=Path(args.supervisor).resolve(strict=True)
    child=Path(args.child).resolve(strict=True)
    uid=os.geteuid()
    if not sys.platform.startswith("linux") or uid==0:
        raise RuntimeError("run from a non-root operator in an OS-owned systemd host")
    user=os.environ.get("USER","")
    if not user or not re.fullmatch(r"[a-z_][a-z0-9_-]*",user):
        raise RuntimeError("operator name cannot identify an OS account")
    group=grp.getgrgid(os.getegid()).gr_name
    unit=f"sg-s6g-guardian-{os.getpid()}-{int(time.time())}"
    if not re.fullmatch(r"[a-z0-9-]+",unit):
        raise RuntimeError("invalid scoped transient service name")
    if property_of(unit+".service","LoadState")!="not-found":
        raise RuntimeError("refuse an existing systemd unit")
    with tempfile.TemporaryDirectory(prefix="s6g-gk-guardian-") as td:
        folder=Path(td)
        save=folder/"original.sqlite"
        os.chmod(folder,0o755)
        full=unit+".service"
        # systemd's OS process-custody path, not a caller-created fake cgroup.
        root=f"/sys/fs/cgroup/system.slice/{full}"
        cmd=["sudo","systemd-run","--unit="+unit,
             "--property=Type=exec","--property=Delegate=yes",
             "--property=KillMode=control-group",
             "--property=TasksMax=64","--property=User="+user,
             "--property=Group="+group,
             "--property=AmbientCapabilities=CAP_SETUID CAP_SETGID",
             "--property=CapabilityBoundingSet=CAP_SETUID CAP_SETGID",
             "--property=NoNewPrivileges=no",
             "--property=StandardOutput=journal",
             str(supervisor),"--save",str(save),"--role","game_host",
             "--timeout-ms","60000",
             "--delegated-cgroup-root",root,
             "--worker-uid","65534","--worker-gid","65534",
             "--build-sha",sha,
             "--",str(child),"escaped-descendant"]
        started=False
        try:
            call(cmd,timeout=20)
            started=True
            until=time.monotonic()+20
            main_pid=0
            descendant=0
            actual_root=""
            while time.monotonic()<until:
                state=property_of(full,"ActiveState")
                value=property_of(full,"MainPID")
                main_pid=int(value) if value.isdecimal() else 0
                actual_root=property_of(full,"ControlGroup")
                match=re.search(r"DETACHED_DESCENDANT=(\d+)",journal(full))
                if state=="active" and main_pid>0 and match:
                    descendant=int(match.group(1))
                    break
                if state in ("failed","inactive"):
                    raise RuntimeError("guardian service exited before detached descendant: "+journal(full)[-1800:])
                time.sleep(0.1)
            if not (main_pid>0 and descendant>0 and not dead(descendant)):
                raise RuntimeError("no genuine live detached descendant before guardian death")
            if actual_root!="/system.slice/"+full:
                raise RuntimeError("systemd cgroup location is not exact unit root: "+actual_root)
            if os.getpgid(descendant)!=descendant:
                raise RuntimeError("descendant did not escape its original process group")
            observed=Path(f"/proc/{descendant}/cgroup").read_text().strip()
            if observed!="0::"+actual_root+"/sgdx-child-"+str(main_pid):
                raise RuntimeError("detached child not in exact original delegated cgroup: "+observed)
            # Kill the genuine SCM-equivalent systemd MainPID, not its worker.
            call(["sudo","systemctl","kill","--kill-who=main","--signal=SIGKILL",full])
            until=time.monotonic()+15
            while time.monotonic()<until:
                if dead(descendant) and property_of(full,"ActiveState") in ("failed","inactive"):
                    break
                time.sleep(0.1)
            if not dead(descendant):
                raise RuntimeError("systemd guardian death left detached cgroup worker executing")
            if property_of(full,"ExecMainStatus")!="9":
                raise RuntimeError("OS journal did not establish genuine guardian SIGKILL")
            child_group=Path("/sys/fs/cgroup"+actual_root)
            if child_group.exists():
                events=(child_group/"cgroup.events").read_text()
                if "populated 0" not in events:
                    raise RuntimeError("systemd original unit cgroup remains populated")
            proof={"schema":"SG-S6GK-SYSTEMD-MAINPID-CUSTODY-01",
                   "source_sha":sha,"status":"hosted_systemd_mainpid_loss_passed_only",
                   "service_unit":full,"control_group":actual_root,
                   "service_mainpid":main_pid,"escaped_descendant_pid":descendant,
                   "original_leaf_membership":observed,
                   "service_mainpid_signaled":9,
                   "detached_descendant_terminated":True,
                   "unit_unpopulated_or_removed":True,
                   "supervisor_sha256":hashlib.sha256(supervisor.read_bytes()).hexdigest(),
                   "child_sha256":hashlib.sha256(child.read_bytes()).hexdigest(),
                   "physical_windows_scm_verified":False,
                   "installed_gameplay_tested":False}
            output=Path(args.output).resolve()
            output.parent.mkdir(parents=True,exist_ok=True)
            tmp=output.with_suffix(".tmp")
            tmp.write_text(json.dumps(proof,indent=2,sort_keys=True)+"\n")
            os.replace(tmp,output)
            print("S6-GK PASS genuine systemd MainPID SIGKILL; detached child killed within owned cgroup")
        finally:
            if started:
                call(["sudo","systemctl","stop",full],required=False)
                call(["sudo","systemctl","reset-failed",full],required=False)

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--supervisor",required=True)
    parser.add_argument("--child",required=True)
    parser.add_argument("--source-sha",required=True)
    parser.add_argument("--output",required=True)
    main(parser.parse_args())
