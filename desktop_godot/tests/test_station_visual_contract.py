#!/usr/bin/env python3
"""PLAY-STATION-02 visual acceptance source contracts (NOT Godot runtime evidence)."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "world/main.gd").read_text(encoding="utf-8")
smoke = (root / "tests/godot_smoke.gd").read_text(encoding="utf-8")
visual = (root / "tests/station_visual_smoke.gd").read_text(encoding="utf-8")
runner = (root / "build_tools/Test-StationGodot.ps1").read_text(encoding="utf-8")
builder = (root / "build_tools/Build-Windows.ps1").read_text(encoding="utf-8")
cmd = (root / "RUN_STATION_PLAYTEST_WINDOWS.cmd").read_text(encoding="utf-8")
player = (root / "world/player_controller.gd").read_text(encoding="utf-8")
station_client = (root / "world/native_station_session.gd").read_text(encoding="utf-8")
cockpit_client = (root / "world/native_player_session.gd").read_text(encoding="utf-8")
station_host = (root.parent / "src/desktop_station_host.cpp").read_text(encoding="utf-8")
terminal = (root / "world/refinery_terminal_ui.gd").read_text(encoding="utf-8")
native_unload_test = (root.parent / "tests/PLAY_STATION_04C_UnloadPipeTests.py").read_text(encoding="utf-8")
action_hints = (root / "world/interaction_prompts.gd").read_text(encoding="utf-8")
action_smoke = (root / "tests/interaction_prompt_smoke.gd").read_text(encoding="utf-8")
boarding_smoke = (root / "tests/boarding_action_pipe_smoke.gd").read_text(encoding="utf-8")
auto_start_smoke = (root / "tests/auto_station_connect_acceptance.gd").read_text(encoding="utf-8")
colony_dock = (root / "world/colony_dock_layout.gd").read_text(encoding="utf-8")
colony_smoke = (root / "tests/colony_dock_smoke.gd").read_text(encoding="utf-8")
utf8_windows = (root.parent / "include/galaxy/NativeWindowsUtf8Argv.hpp").read_text(encoding="utf-8")
cockpit_host_source = (root.parent / "src/desktop_player_host.cpp").read_text(encoding="utf-8")
sgdx_host_source = (root.parent / "src/server_dx_supervisor.cpp").read_text(encoding="utf-8")
unicode_pipe_test = (root.parent / "tests/FP3D_WinUtf8NativePathTests.py").read_text(encoding="utf-8")
player_controller = (root / "world/player_controller.gd").read_text(encoding="utf-8")

checks = {
    "avatar-height-matches-capsule": "const AVATAR_CENTER_HEIGHT_M := 0.90" in main,
    "no-H-in-normal-player-launch": (
        'call_deferred("_open_saved_station_automatically")' in main
        and "DisplayServer.get_name() != \"headless\"" in main
        and 'not OS.get_cmdline_args().has("--script")' in main
        and 'scope_label.text = "Connecting to original saved world..."' in main
        and "func _retry_original_station()" in main
        and 'retry_station_button.name = "RetryStationConnection"' in main
        and 'retry_station_button.visible = false' in main
        and 'native_station_session.stop("Station test session stopped"' not in main
    ),
    "original-boarding-auto-cockpit-one-sqlite-writer": (
        "func _enter_cockpit_after_verified_board()" in main
        and 'native_station_session.stop("Native boarding committed; transferring verified SQLite writer", true)' in main
        and '_schedule_native_handoff("ship", exiting_pid)' in main
        and "last_station_player_state == 0 and _player_session_autoconnect" in main
        and "SG_FP3D_CI_AUTOSTART" in auto_start_smoke
        and "for index in range(12):" in auto_start_smoke
        and 'game.native_station_session.interact_hangar()' in auto_start_smoke
        and 'Callable(self, "_ship_ready")' in auto_start_smoke
        and 'Callable(self, "_colony_returned")' in auto_start_smoke
        and 'PackedStringArray(["UND"])' in auto_start_smoke
        and 'PackedStringArray(["DCK"])' in auto_start_smoke
        and "func _exit_docked_ship_after_verified_station_person()" in main
        and "_colony_return_pending" in main
        and "_colony_return_armed" in main
    ),
    "focused-keyboard-flight-with-visible-mouse": (
        "func _collect_native_pilot_actions() -> PackedStringArray:" in player
        and "if get_window().has_focus():\n\\t\\t\\tif native_seated and native_in_ship:".replace("\\t", chr(9)) in player
        and 'elif not native_in_ship and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:' in player
        and "if Input.mouse_mode == Input.MOUSE_MODE_CAPTURED and \\" in player
        and 'pilot_actions.append("FWD")' in player
        and 'pilot_actions.append("LEFT")' in player
        and 'Input.mouse_mode = Input.MOUSE_MODE_VISIBLE' in action_smoke
        and 'free_cursor_actions.has("FWD")' in action_smoke
        and 'free_cursor_actions.has("LEFT")' in action_smoke
        and 'docked_actions.has("UND")' in action_smoke
        and 'unseated player generated forbidden original flight intent' in action_smoke
        and 'on-foot station player generated forbidden ship thrust intent' in action_smoke
        and 'KEYBOARD FLIGHT READY' in main
        and 'PILOT PAUSED' in main
        and 'PILOT WAIT' in main
    ),
    "pilot-only-auto-seat-after-original-board": (
        "func _ensure_native_pilot_role() -> void:" in main
        and 'not native_player_session._initial_ship_seen' in main
        and 'native_player_session.toggle_seat()' in main
        and '_pilot_auto_seat_attempted = true' in main
        and 'func use_native_ship_custody(aboard: bool)' in player
        and 'elif not native_in_ship:' in player
        and 'func _on_native_player_intent(heading: Vector3)' in main
        and 'native_player_session.submit_motion(heading)' not in main
        and 'Callable(self, "_seat_ready")' in auto_start_smoke
        and '"E escaped the locked pilot seat while docked"' in auto_start_smoke
        and '"E or third-person camera rule allowed ship exploration in space"' in auto_start_smoke
        and '"Godot still sent cabin MOVE despite original pilot-only custody"' in auto_start_smoke
    ),
    "pilot-only-third-person-and-one-verified-docked-E": (
        'var exterior := not in_station' in main
        and 'func _complete_docked_disembark_after_native_seat_release()' in main
        and '_colony_return_pending = true' in main
        and 'call_deferred("_complete_docked_disembark_after_native_seat_release")' in main
        and '_schedule_native_handoff("station", outgoing_pid)' in main
        and 'call_deferred("_exit_docked_ship_after_verified_station_person")' in main
        and 'native_station_session.interact_hangar()' in main
        and 'P developer stop is disabled in gameplay' in main
        and 'Pilot-only third-person camera is fixed while aboard' in main
        and 'THIRD-PERSON FLIGHT' in action_hints
        and '"docked ship falsely showed an explorable cabin or a first-person camera"' in action_smoke
        and '"real redocking exposed a walkable cabin or released the pilot seat"' in auto_start_smoke
    ),
    "unexpected-host-exit-writes-bounded-diagnostic-not-world": (
        "func _unexpected_native_exit()" in station_client
        and 'user://sg_station_last_failure.json' in station_client
        and "SG_FP3D_OPERATOR_CAPTURE_DIR" in station_client
        and "native_error.substr(0, 300)" in station_client
        and "stop(failure)" in station_client
    ),
    "station-host-supervised-project-binary": "SGDX_LAUNCH.native_host_path(binary)" in station_client and 'SGDX_LAUNCH.supervised_command(save, "godot_station", exe,' in station_client and 'OS.execute_with_pipe(spec["path"], spec["args"], false)' in station_client,
    "cockpit-host-supervised-project-binary": "SGDX_LAUNCH.native_host_path(binary)" in cockpit_client and 'SGDX_LAUNCH.supervised_command(save, "godot_ship", path, PackedStringArray([save]))' in cockpit_client and 'OS.execute_with_pipe(spec["path"], spec["args"], false)' in cockpit_client,
    "refuse-unguarded-native-launch": "refusing unsupervised" in station_client and "refusing unsupervised" in cockpit_client,
    "windows-utf8-native-sqlite-and-private-sgdx-paths": (
        "WideCharToMultiByte(" in utf8_windows
        and "WC_ERR_INVALID_CHARS" in utf8_windows
        and "int wmain(int argc,wchar_t** argv)" in station_host
        and "int wmain(int argc,wchar_t** argv)" in cockpit_host_source
        and "fs::u8path(save_utf8)" in station_host
        and "fs::u8path(save_utf8)" in cockpit_host_source
        and "PersistentWorld world(save_utf8" in station_host
        and "PersistentWorld world(save_utf8" in cockpit_host_source
        and "std::filesystem::u8path(args.save)" in sgdx_host_source
        and "Colony — Departure Dock.sqlite" in unicode_pipe_test
        and "White Ship — Cockpit.sqlite" in unicode_pipe_test
    ),
    "colony-origin-matches-original-person": (
        "const COLONY_SPAWN := Vector3(1.6, 0.0, 0.0)" in colony_dock
        and "const DOCK_GATE := Vector3(0.0, 0.0, 0.0)" in colony_dock
        and "COLONY_DOCK_LAYOUT.build(station_space)" in main
        and "original C++ PERSON genesis" in colony_smoke
    ),
    "colony-to-dock-transition-composition": (
        'colony.name = "ColonyArrival"' in colony_dock
        and 'corridor.name = "ColonyToDockWalkway"' in colony_dock
        and 'dock.name = "SpacefaringDock"' in colony_dock
        and '"Colony arrival wayfinding"' in colony_dock
        and '"Ship embarkation ramp"' in colony_dock
        and '"Departure dock label"' in colony_dock
        and '"Owned ship dark cockpit canopy"' in colony_dock
        and "FP3D-COLONY-01 REAL GODOT PASS" in colony_smoke
    ),
    "visual-regions-never-authorize-boarding": (
        "VISUAL-ONLY" in colony_dock
        and "var node := MeshInstance3D.new()" in colony_dock
        and "var sign := Label3D.new()" in colony_dock
        and not any(forbidden in colony_dock for forbidden in (
            "CollisionShape3D.new(", "CollisionObject3D.new(", "StaticBody3D.new(",
            "Area3D.new(", "CharacterBody3D.new(", "RigidBody3D.new(",
            ".connect(", "interact_hangar(", "station_player_action(",
            "submit_walk(", "submit_flight(")
        )
        and 'find_children("*", "CollisionObject3D", true, false)' in colony_smoke
        and "eligible_ship" in colony_smoke
        and 'return "Press %s to Board' in action_hints
        and 'return "COLONY → DOCK' in action_hints
        and 'absf(person.global_position.x - 1.6)' in colony_smoke
    ),
    "docked-ship-near-authoritative-hatch": (
        'Vector3(0, 1.34, -4.90)' in main
        and 'Vector3(0.0, 0.084, -1.9)' in colony_dock
        and 'const SHIP_BERTH := Vector3(0.0, 0.0, -4.9)' in colony_dock
        and '"Station_Hangar_Entry_At_Zero"' in main
        and '"Docked owned ship hull (visual)"' in colony_smoke
    ),
    "station-room": '"StationInterior_Prototype"' in main,
    "cabin-room": '"ShipInterior_Prototype"' in main,
    "hatch": '"Station_Hangar_Entry_At_Zero"' in main,
    "real-highlighted-nonblocking-hatch": (
        "hatch_color.albedo_color = Color(0.08, 0.89, 0.96, 1.0)" in main
        and 'hatch.name = "Station_Hangar_Entry_At_Zero"' in main
        and "MeshInstance3D.new()" in main
    ),
    "visible-native-hatch-not-under-floor": (
        'hatch.position = Vector3(0.0, 0.0775, 0.0)' in main
        and 'beacon.name = "BoardingHatch_WorldSign_VisualOnly"' in main
        and "hatch.position.y < 0.065" in action_smoke
    ),
    "station-gray-ship-white-hud-separation": (
        "const STATION_GRAY := Color(0.47, 0.51, 0.55, 1.0)" in main
        and "const SHIP_WHITE := Color(0.96, 0.97, 0.99, 1.0)" in main
        and "white_hull" in main
        and 'panel.name = "SystemStatusPanel"' in main
        and 'flight_panel.name = "FlightTelemetryPanel"' in main
        and "flight_panel.visible = not in_station" in main
        and "fps_label.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_LEFT)" in main
    ),
    "native-person-and-camera-relative-hatch-direction": (
        "static func hatch_direction_key(person: Dictionary, player_basis: Basis)" in action_hints
        and "player_basis.inverse() * toward" in action_hints
        and 'display += "  |  Hold " + key_to_hatch + " toward hatch"' in main
        and 'hatch_direction_key(spawn, Basis.IDENTITY) != "A"' in action_smoke
        and 'hatch_direction_key(spawn, Basis(Vector3.UP, PI / 2.0)) != "W"' in action_smoke
    ),
    "E-press-is-focus-only-not-mouse-capture": (
        'get_window().has_focus() and Input.is_action_just_pressed("fps_inspect")' in player_controller
        and "native_seat_intent.emit()" in player_controller
    ),
    "real-native-command-rate-gate-not-bypassed": (
        "const NATIVE_HANGAR_COMMAND_GAP_MS := 130" in station_client
        and "_queued_hangar_ready_msec" in station_client
        and "Time.get_ticks_msec() + 75" in station_client
        and "func _dispatch_queued_hangar_if_ready()" in station_client
        and "One boarding request already queued" in station_client
        and "E sent BEFORE native command rate interval" in boarding_smoke
        and "second E bypassed exactly-once queued BOARD cooldown" in boarding_smoke
    ),
    "native-E-after-WALK-single-use-custody": (
        "var _pending_walk := 0" in station_client
        and "var _queued_hangar_intent := false" in station_client
        and "_pending == _pending_walk" in station_client
        and "last_walk_confirmed and _queued_hangar_intent" in station_client
        and 'interact_hangar()' in station_client
        and "BOARD|77|10|3|5" in boarding_smoke
        and "station-custody-or-distance" in boarding_smoke
    ),
    "native-custody-only-interaction-hints": (
        "const NATIVE_HATCH_AXIS_MM := 600" in action_hints
        and "absi(x) <= NATIVE_HATCH_AXIS_MM" in action_hints
        and "absi(z) <= NATIVE_HATCH_AXIS_MM" in action_hints
        and "eligible > 0" in action_hints
        and "if station_active:" in action_hints
        and "if handoff_pending:" in action_hints
        and "REFINERY_RADIUS_MM := 1200" in action_hints
    ),
    "HUD-native-action-guidance-not-second-physics": (
        'const INTERACTION_PROMPTS := preload("res://world/interaction_prompts.gd")' in main
        and 'prompt.name = "InteractionPrompt"' in main
        and 'interaction_label.name = "InteractionAction"' in main
        and "INTERACTION_PROMPTS.hint(" in main
        and "native_station_session._person" in main
        and "native_player_session._sequence" in main
        and "prompt.mouse_filter = Control.MOUSE_FILTER_IGNORE" in main
    ),
    "interaction-smoke-must-reach-flight-and-pilot-assertions": (
        (chr(9)*2 + "return" + chr(10))*2 not in action_smoke
        and '"the seated native ship did not advertise mandatory third-person view"' in action_smoke
        and '"in-space ship camera failed permanent third-person rule"' in action_smoke
        and 'FP3D-PILOT-01 REAL GODOT VISUAL PASS:' in action_smoke
        and "quit(0)" in action_smoke
    ),
    "live-engine-has-interaction-smoke": (
        "FP3D-UX-02 real Godot visual/input PASS" in action_smoke
        and "600" in action_smoke
        and "eligible_ship" in action_smoke
        and "Connecting to saved station" in action_smoke
    ),
    "separate-physics": "for body in station_space.get_children()" in main and "for body in ship_space.get_children()" in main,
    "no-invisible-collision": "body.collision_layer = 0 if in_station or exterior else 1" in main and "body.collision_layer = 1 if in_station else 0" in main and "body.collision_mask = 0 if in_station or exterior else 1" in main,
    "native-station-frame-only": "_show_native_location(last_station_player_state == 4)" in main,
    "native-pose-frame-only": (
        '_record_native_projection("POSE", pose)' in main
        and 'player.use_native_ship_custody(true)' in main
        and 'player.use_native_seat(bool(pose["seated"]))' in main
        and '_show_native_location(false)' in main
    ),
    "native-ship-telemetry": '_record_native_projection("SHIP", ship)' in main,
    "logged-client-evidence-not-second-save": '"godot-observed-native-frame"' in main and "sg_station_visual_acceptance.jsonl" in main,
    "bounded-client-evidence": "1048576" in main,
    "repaired-recursive-collider-smoke": 'station_space.find_children("*", "StaticBody3D", true, false)' in smoke,
    "original-smoke-no-root-only-assertion": "for node in root_scene.get_children():" not in smoke,
    "scene-visual-smoke": '"station/cabin real collider inventory incomplete"' in visual,
    "godot-472-dynamic-crate-shape-by-real-type": (
        "for child in native_crate.get_children()" in visual
        and "child is CollisionShape3D" in visual
        and "var native_shape: CollisionShape3D = null" in visual
        and 'native_crate.get_node_or_null("CollisionShape3D")' not in visual
        and "native_box.size.distance_to(Vector3(0.8,1.6,1.0))" in visual
    ),
    "synthetic-test-honesty": "Frames are SYNTHETIC" in visual,
    "visual-headless-in-export": "'station-visual-scene-smoke'" in builder,
    "windows-runner-builds-exact-native": "native-build" in runner and "galaxy_desktop_station_host" in runner and "galaxy_desktop_player_host" in runner,
    "windows-native-pipe-smoke": "Test-NativeStation.ps1" in runner,
    "installed-godot-only": "4.7.2.stable" in runner and "No editor is downloaded" in runner,
    "windows-godot-real-process": "station_visual_smoke.gd" in runner and "Start-Process -FilePath $GodotExe" in runner,
    "user-invoked-hardware-play": "NEEDS-OBSERVATION" in runner,
    "double-click-entry": "Launch-PrebuiltStation.ps1" in cmd,
    "prebuilt-launcher-replaces-local-compiler": "Test-StationGodot.ps1" not in cmd,
    "prebuilt-launcher-source": (root / "build_tools/Launch-PrebuiltStation.ps1").is_file(),
    "suppress-fake-godot-walking": "if authoritative_mode:" in player and "native_motion_intent.emit(direction)" in player,
    "04c-original-unload-not-client-mass": 'const bool unload=f.size()==5&&f[0]=="UNLOAD"' in station_host and "c.action=Action::UnloadOre" in station_host and "c.amount=1;" in station_host and "station_launch::available_ship" in station_host,
    "04c-host-sqlite-commit": 'world.step();' in station_host and 'result=event.result==Outcome::Applied?3:4;' in station_host and "station_refinery_protocol::make_snapshot" in station_host,
    "04c-godot-request-bound-to-original-person": 'int(_person.get("eligible_ship", 0))' in station_client and '"UNLOAD|%d|%d|%d|%d"' in station_client,
    "04c-separate-tonne-and-kg-receipts": '"unloaded_tonnes"' in station_client and '"transferred_kg"' in station_client and 'result not in [0, 1, 2, 3, 4]' in station_client,
    "04c-godot-terminal-wire": 'refinery_terminal.unload_requested.connect(_on_native_unload_requested)' in main and 'native_station_session.unload_owned_ship_tonne()' in main and "signal unload_requested()" in terminal,
    "04c-no-optimistic-inventory": 'unload_requested.emit()' in terminal and '_status.text = "Requesting one-tonne physical unload; awaiting original SQLite receipt..."' in terminal,
    "04c-native-fixture-failure-recovery": "--04c-fixture" in native_unload_test and "PRAGMA integrity_check" in native_unload_test and "--03c-verify" in native_unload_test and "no-owned-docked-ship-with-ore" in native_unload_test
}
for name, ok in checks.items():
    assert ok, name
print(f"PLAY-STATION-02 visual source contracts PASS {len(checks)}/{len(checks)} (Godot engine and Windows runtime NOT executed by this test)")
