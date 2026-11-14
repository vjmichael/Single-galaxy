#!/usr/bin/env python3
"""Source-only admission check; native subprocess and Godot engine are separate gates."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
launcher=(root/"world/sgdx_process_launch.gd").read_text(encoding="utf-8")
sources={
    "ship":(root/"world/native_player_session.gd").read_text(encoding="utf-8"),
    "station":(root/"world/native_station_session.gd").read_text(encoding="utf-8"),
    "observer":(root/"world/native_session.gd").read_text(encoding="utf-8"),
}
supervisor=(root.parent/"src/server_dx_supervisor.cpp").read_text(encoding="utf-8")
dump_capture=(root.parent/"include/galaxy/ServerWindowsMinidump.hpp").read_text(encoding="utf-8")
checks={
    "windows Unicode argv survives real Godot em-dash app userdata":(
        "int wmain(int argc,wchar_t** argv)" in supervisor
        and "WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS" in supervisor
        and "static std::string utf8_argument(" in supervisor
        and "utf8_argument(argv[i])" in supervisor
        and "MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS" in supervisor
        and "#else\nint main(int argc,char** argv)" in supervisor
        and 'config/name="Single-Galaxy — Graybox FPS"' in (root/"project.godot").read_text(encoding="utf-8")
    ),
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
    "real Windows DbgHelp retries minimal SECOND-CHANCE dump if thread-info fails":(
        "MiniDumpNormal|MiniDumpWithThreadInfo" in dump_capture
        and "file,MiniDumpNormal," in dump_capture
        and "SetEndOfFile(file)" in dump_capture
        and "capture_second_chance(" in supervisor
        and "DEBUG_ONLY_THIS_PROCESS" in supervisor
    ),
    "sensitive Windows crash bytes never enter logs; stage-only failure receipt":(
        "std::string* failure=nullptr" in dump_capture
        and 'return fail("private_directory_acl"' in dump_capture
        and 'return fail("dbghelp_threadinfo_then_normal"' in dump_capture
        and "dump_failure.empty()" in supervisor
        and "crash-time minidump recorded" in supervisor
        and "ScopedPrivateSecurity security" in dump_capture
    ),
    "expired three-second PID cannot hide live guardians":all(
        "if _last_shutdown_pid > 0:" in sources[name] and
        "if OS.is_process_running(_last_shutdown_pid):" in sources[name] and
        "return _last_shutdown_pid" in sources[name] and
        "refusing overlapping native session" in sources[name] and
        "_last_shutdown_msec < 3000" not in sources[name]
        for name in ("ship","station")),
    "expired handoff never enables unauthoritative walking":(
        "player.use_native_authority(false)" not in
        (root/"world/main.gd").read_text(encoding="utf-8").split(
            'if Time.get_ticks_msec() - _handoff_started_msec > 3000:',1)[1]
            .split('var destination := _handoff_target',1)[0]),
}
for key,ok in checks.items():
    assert ok,key
print(f"SGDX-03 source-only Godot/supervisor contracts PASS {len(checks)}; native gates required")
