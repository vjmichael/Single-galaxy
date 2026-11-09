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

checks = {
    "station-room": '"StationInterior_Prototype"' in main,
    "cabin-room": '"ShipInterior_Prototype"' in main,
    "hatch": '"Station_Hangar_Entry_At_Zero"' in main,
    "separate-physics": "for body in station_space.get_children()" in main and "for body in ship_space.get_children()" in main,
    "no-invisible-collision": "body.collision_layer = 0 if in_station else 1" in main and "body.collision_layer = 1 if in_station else 0" in main,
    "native-station-frame-only": "_show_native_location(last_station_player_state == 4)" in main,
    "native-pose-frame-only": '_record_native_projection("POSE", pose)' in main and '_show_native_location(false) # verified original owned ship pose' in main,
    "native-ship-telemetry": '_record_native_projection("SHIP", ship)' in main,
    "logged-client-evidence-not-second-save": '"godot-observed-native-frame"' in main and "sg_station_visual_acceptance.jsonl" in main,
    "bounded-client-evidence": "1048576" in main,
    "repaired-recursive-collider-smoke": 'station_space.find_children("*", "StaticBody3D", true, false)' in smoke,
    "original-smoke-no-root-only-assertion": "for node in root_scene.get_children():" not in smoke,
    "scene-visual-smoke": '"station/cabin real collider inventory incomplete"' in visual,
    "synthetic-test-honesty": "Frames are SYNTHETIC" in visual,
    "visual-headless-in-export": "'station-visual-scene-smoke'" in builder,
    "windows-runner-builds-exact-native": "native-build" in runner and "galaxy_desktop_station_host" in runner and "galaxy_desktop_player_host" in runner,
    "windows-native-pipe-smoke": "Test-NativeStation.ps1" in runner,
    "installed-godot-only": "4.7.2.stable" in runner and "No editor is downloaded" in runner,
    "windows-godot-real-process": "station_visual_smoke.gd" in runner and "Start-Process -FilePath $GodotExe" in runner,
    "user-invoked-hardware-play": "NEEDS-OBSERVATION" in runner,
    "double-click-entry": "Test-StationGodot.ps1" in cmd,
    "suppress-fake-godot-walking": "if authoritative_mode:" in player and "native_motion_intent.emit(direction)" in player
}
for name, ok in checks.items():
    assert ok, name
print(f"PLAY-STATION-02 visual source contracts PASS {len(checks)}/{len(checks)} (Godot engine and Windows runtime NOT executed by this test)")
