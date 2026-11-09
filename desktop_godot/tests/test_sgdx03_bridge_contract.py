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
    "expired three-second PID cannot hide live guardians":all(
        "if _last_shutdown_pid > 0:" in sources[name] and
        "if OS.is_process_running(_last_shutdown_pid):" in sources[name] and
        "return _last_shutdown_pid" in sources[name] and
        "refusing overlapping native session" in sources[name] and
        "_last_shutdown_msec < 3000" not in sources[name]
        for name in ("ship","station")),
    "expired handoff never enables unauthoritative walking":(
        "player.use_native_authority(false)" not in
        (root/"world/main.gd").read_text().split(
            'if Time.get_ticks_msec() - _handoff_started_msec > 3000:',1)[1]
            .split('var destination := _handoff_target',1)[0]),
}
for key,ok in checks.items():
    assert ok,key
print(f"SGDX-03 source-only Godot/supervisor contracts PASS {len(checks)}; native gates required")
