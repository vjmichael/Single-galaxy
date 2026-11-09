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

checks = {
    "avatar-height-matches-capsule": "const AVATAR_CENTER_HEIGHT_M := 0.90" in main,
    "station-host-supervised-project-binary": "SGDX_LAUNCH.native_host_path(binary)" in station_client and 'SGDX_LAUNCH.supervised_command(save, "godot_station", exe,' in station_client and 'OS.execute_with_pipe(spec["path"], spec["args"], false)' in station_client,
    "cockpit-host-supervised-project-binary": "SGDX_LAUNCH.native_host_path(binary)" in cockpit_client and 'SGDX_LAUNCH.supervised_command(save, "godot_ship", path, PackedStringArray([save]))' in cockpit_client and 'OS.execute_with_pipe(spec["path"], spec["args"], false)' in cockpit_client,
    "refuse-unguarded-native-launch": "refusing unsupervised" in station_client and "refusing unsupervised" in cockpit_client,
    "station-room": '"StationInterior_Prototype"' in main,
    "cabin-room": '"ShipInterior_Prototype"' in main,
    "hatch": '"Station_Hangar_Entry_At_Zero"' in main,
    "separate-physics": "for body in station_space.get_children()" in main and "for body in ship_space.get_children()" in main,
    "no-invisible-collision": "body.collision_layer = 0 if in_station or exterior else 1" in main and "body.collision_layer = 1 if in_station else 0" in main and "body.collision_mask = 0 if in_station or exterior else 1" in main,
    "native-station-frame-only": "_show_native_location(last_station_player_state == 4)" in main,
    "native-pose-frame-only": '_record_native_projection("POSE", pose)' in main and '_show_native_location(false) # verified original owned ship pose' in main,
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
