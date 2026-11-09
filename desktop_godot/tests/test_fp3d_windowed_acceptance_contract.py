#!/usr/bin/env python3
"""FP3D-WIN-01 source wiring only; native Windows/Godot runtime is a separate gate."""
from pathlib import Path

project = Path(__file__).resolve().parents[1]
root = project.parent
scene = (project / "tests/fp3d_env01_live_scene_acceptance.gd").read_text()
game = (project / "world/main.gd").read_text()
station = (project / "world/native_station_session.gd").read_text()
flight = (project / "world/native_player_session.gd").read_text()
gui_launcher = (project / "build_tools/Launch-PrebuiltStation.ps1").read_text()
package = (project / "build_tools/Verify-PrecompiledPackage.ps1").read_text()
windowed = (project / "build_tools/Test-WindowedNativeGodot.ps1").read_text()
guide = (root / "docs/engineering/FP3D_WINDOWS_PHYSICAL_ACCEPTANCE.md").read_text()

checks = {
    "real-window-not-headless": (
        'DisplayServer.get_name().to_lower() == "headless"' in scene
        and "await RenderingServer.frame_post_draw" in scene
        and "root.get_texture().get_image()" in scene
    ),
    "four-independently-hashed-godot-render-frames": (
        all(name in scene for name in (
            "01-docked-cabin", "02-exterior-flight",
            "03-native-rock-impact", "04-restarted-world"))
        and "frame.save_png(absolute)" in scene
        and "FileAccess.get_sha256(absolute)" in scene
        and "samples.size() < 4" in scene
    ),
    "live-original-not-synthetic-ship-rock": (
        'session.start(path)' in scene and
        "session.ship_state_received.connect(_on_ship)" in scene and
        "session.asteroid_contact_received.connect(_on_rock)" in scene and
        "session.submit_flight(PackedStringArray" in scene
    ),
    "native-restart-same-world-and-no-replayed-impact": (
        "restored.start(path)" in scene and
        'name in ["ship", "tick", "gen"' in scene and
        'int(resumed_rock["impact_rock"]) != 0' in scene and
        "SGDX supervised original host failed to release" in scene
    ),
    "native-authority-single-writer": (
        'SGDX_LAUNCH.supervised_command(save, "godot_ship"' in flight
        and 'SGDX_LAUNCH.supervised_command(save, "godot_station"' in station
        and "refusing unsupervised" in flight
        and "refusing unsupervised" in station
    ),
    "windowed-ci-run-cannot-revert-to-headless": (
        "'--rendering-method','gl_compatibility'" in windowed
        and "'--script','res://tests/fp3d_env01_live_scene_acceptance.gd'" in windowed
        and "'--headless'" not in windowed
        and "Windowed Godot/native test timed out" in windowed
    ),
    "reopened-sqlite-collision-is-source-verified": (
        "$after.ship.tick -ne $impact.ship.tick" in windowed
        and "$after.rock.ore_tonnes -ne $impact.rock.ore_tonnes" in windowed
        and "$impact.rock.impact_tick -ne $impact.ship.tick" in windowed
        and "Get-FileHash -LiteralPath $file -Algorithm SHA256" in windowed
    ),
    "actual-delivered-zip-verification": (
        "Expand-Archive -LiteralPath $archivePath" in package
        and "Verify-Pe64" in package
        and "SGDX_BUILD_SHA.txt" in package
        and "sqlite3.dll" in package
        and "no_private_world_or_crash_dumps" in package
    ),
    "double-click-visual-evidence-explicit-opt-in": (
        "KEY_F11" in game and
        "operator-viewport-receipts.jsonl" in game and
        "SG_FP3D_OPERATOR_CAPTURE_DIR" in game and
        "get_viewport().get_texture().get_image()" in game and
        "SG_FP3D_OPERATOR_CAPTURE_DIR" in gui_launcher
    ),
    "not-falsely-accepted-on-window-close": (
        "WINDOW-CLOSED-NEEDS-MANUAL-OBSERVATION" in gui_launcher
        and "human_gpu_visual_approval" in scene
        and "physical Windows" in guide
    ),
}
for label, good in checks.items():
    assert good, "FP3D-WIN-01 source contract failed: " + label
print(f"FP3D-WIN-01 source-only contracts PASS {len(checks)}/{len(checks)}; "
      "actual Windows MSVC, Godot pixels, private MDMP and physical GPU still require native evidence")
