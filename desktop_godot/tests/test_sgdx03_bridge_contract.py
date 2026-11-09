#!/usr/bin/env python3
"""Source-only admission check; native subprocess and Godot engine are separate gates."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
launcher=(root/"world/sgdx_process_launch.gd").read_text()
sources={
    "ship":(root/"world/native_player_session.gd").read_text(),
    "station":(root/"world/native_station_session.gd").read_text(),
    "observer":(root/"world/native_session.gd").read_text(),
}
supervisor=(root.parent/"src/server_dx_supervisor.cpp").read_text()
checks={
    "all paths use strict SGDX helper":all(
        'SGDX_LAUNCH.supervised_command(' in source for source in sources.values()),
    "no direct native writer launched":all(
        'OS.execute_with_pipe(spec["path"], spec["args"], false)' in source
        for source in sources.values()),
    "fail closed without SGDX binary":'return {} # never silently weaken supervision' in launcher,
    "real original pipes not serialized to files":"--interactive" in launcher and
        "args.append(host)" in launcher and "args.append_array(host_args)" in launcher,
    "per-world diagnostic sidecar":"--save" in launcher and '"--role"' in launcher,
    "Windows memory dumps explicit":"SGDX_WINDOWS_MINIDUMP" in launcher,
    "normal writer QUIT and strong fault kill":all(
        'store_line("QUIT")' in sources[name] and 'OS.kill(_pid)' in sources[name]
        for name in ("ship","station")) and
        'OS.kill(_pid)' in sources["observer"],
    "interactive mode remains fully supervised":'args.interactive?INFINITE' in supervisor
        and "!args.interactive" in supervisor
        and "JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE" in supervisor
        and "PR_SET_PDEATHSIG" in supervisor,
    "opt-in Windows second-chance dump":"capture_second_chance(" in supervisor
        and "DEBUG_ONLY_THIS_PROCESS" in supervisor,
}
for key,ok in checks.items():
    assert ok,key
print(f"SGDX-03 source-only Godot/supervisor contracts PASS {len(checks)}; native gates required")
