#!/usr/bin/env python3
"""FP3D-WIN-01 source wiring only; native Windows/Godot runtime is a separate gate."""
from pathlib import Path

project = Path(__file__).resolve().parents[1]
root = project.parent
scene = (project / "tests/fp3d_env01_live_scene_acceptance.gd").read_text(encoding="utf-8")
game = (project / "world/main.gd").read_text(encoding="utf-8")
refinery = (project / "world/refinery_terminal_ui.gd").read_text(encoding="utf-8")
station = (project / "world/native_station_session.gd").read_text(encoding="utf-8")
flight = (project / "world/native_player_session.gd").read_text(encoding="utf-8")
gui_launcher = (project / "build_tools/Launch-PrebuiltStation.ps1").read_text(encoding="utf-8")
package = (project / "build_tools/Verify-PrecompiledPackage.ps1").read_text(encoding="utf-8")
windowed = (project / "build_tools/Test-WindowedNativeGodot.ps1").read_text(encoding="utf-8")
guide = (root / "docs/engineering/FP3D_WINDOWS_PHYSICAL_ACCEPTANCE.md").read_text(encoding="utf-8")
engine_license = (project / "third_party/GODOT_LICENSE.txt").read_text(encoding="utf-8")
tamper = (project / "build_tools/Test-BundledRuntimeTamper.ps1").read_text(encoding="utf-8")
root_launcher = (project / "START_SINGLE_GALAXY_WINDOWS.cmd").read_text(encoding="utf-8")

checks = {
    "godot-472-strict-types-in-actual-ui": (
        "var station_authorized: bool =" in game
        and "var flight_authorized: bool =" in game
        and 'var products: Dictionary = snapshot.get("minerals", {})' in refinery
        and "var names: Array = products.keys()" in refinery
        and "var products := snapshot.get(" not in refinery
        and "var names := products.keys()" not in refinery
    ),
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
        '_submit_visible_cursor_keyboard_flight("ship_thrust_forward", "FWD")' in scene
        and '_submit_visible_cursor_keyboard_flight("ship_yaw_left", "LEFT")' in scene
        and '_submit_visible_cursor_keyboard_flight("ship_yaw_right", "RIGHT")' in scene
        and '_game.player.native_ship_intents.emit(intents)' in scene
        and "committed_ship" in scene
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
        and "'--headless'," not in windowed
        and "Windowed Godot/native test timed out" in windowed
    ),
    "docked-original-pilot-must-stay-third-person-on-real-render": (
        "$seen['01-docked-cabin'].camera_current -ne $true" in windowed
        and "$seen['01-docked-cabin'].player_camera_current -ne $false" in windowed
        and "Authentic docked native pilot must remain third-person" in windowed
        and "$seen['01-docked-cabin'].player_camera_current -ne $true" not in windowed
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
    "default-uses-verified-bundled-godot-no-picker": (
        "runtime\\Godot_v4.7.2-stable_win64.exe" in gui_launcher
        and "GODOT_RUNTIME_SHA256.txt" in gui_launcher
        and "Get-LocalSha256 $GodotExe" in gui_launcher
        and "System.Security.Cryptography.SHA256" in gui_launcher
        and "System32\\WindowsPowerShell\\v1.0\\Modules" in gui_launcher
        and "$PSVersionTable.PSEdition -eq 'Desktop'" in gui_launcher
        and "$hasher.Dispose()" in gui_launcher
        and "$stream.Dispose()" in gui_launcher
        and "(Get-FileHash " not in gui_launcher
        and "Developer -GodotExe override selected" in gui_launcher
        and "OpenFileDialog" not in gui_launcher
        and "Get-Command $name" not in gui_launcher
        and "SINGLE_GALAXY_GODOT_EXE" not in gui_launcher
    ),
    "package-requires-bundled-pe64-engine-and-mit": (
        "Verify-Pe64 $engine" in package
        and "GODOT_RUNTIME_SHA256.txt" in package
        and "LICENSE.txt" in package
        and "bundled_official_godot_pe64_sha256_license" in package
        and "Godot Engine contributors" in engine_license
        and "Permission is hereby granted, free of charge" in engine_license
    ),
    "extracted-zip-bundled-engine-preflight-does-not-claim-visual-pass": (
        "[switch]$VerifyOnly" in gui_launcher
        and "$VerifyOnly -and $SkipHeadlessSmoke" in gui_launcher
        and "BUNDLED-PACKAGE-HEADLESS-PREFLIGHT-VERIFIED-NO-GPU-CLAIM" in gui_launcher
        and "WINDOW-CLOSED-NEEDS-MANUAL-OBSERVATION" in gui_launcher
        and "SingleGalaxy_Station_Playtest_Evidence.zip" in gui_launcher
    ),
    "actual-windows-powershell5-clean-bundle-not-only-pwsh7": (
        "powershell-5_1-clean-stdout.log" in tamper
        and "powershell-5_1-clean-preflight.json" in tamper
        and "BUNDLED-PACKAGE-HEADLESS-PREFLIGHT-VERIFIED-NO-GPU-CLAIM" in tamper
        and "clean_powershell5_preflight='PASS'" in tamper
        and "Real Windows PowerShell 5.1 could not run the unmodified bundled" in tamper
    ),
    "windows-bundled-engine-mismatch-refused": (
        "WindowsPowerShell\\v1.0\\powershell.exe" in tamper
        and "Set-Content -LiteralPath $manifest -Value $wrong" in tamper
        and "Bundled Godot SHA-256 does not match release-derived package manifest" in tamper
        and "Set-Content -LiteralPath $manifest -Value $original" in tamper
        and "EXPECTED_HASH_REFUSAL_VERIFIED" in tamper
    ),
    "self-contained-one-double-click-zip-root": (
        "START_SINGLE_GALAXY_WINDOWS.cmd" in package
        and "RUN_STATION_PLAYTEST_WINDOWS.cmd" in root_launcher
        and "PowerShell.exe" not in root_launcher
        and "curl" not in root_launcher.lower()
        and "START_SINGLE_GALAXY_WINDOWS.cmd" in guide
    ),
    "official-console-companion-keeps-real-cli-exit-and-frontend-GUI-separate": (
        "runtime\\Godot_v4.7.2-stable_win64_console.exe" in gui_launcher
        and "GODOT_CONSOLE_SHA256.txt" in gui_launcher
        and "Get-LocalSha256 $CliGodotExe" in gui_launcher
        and "$info.FileName=$CliGodotExe" in gui_launcher
        and "$child.Start()" in gui_launcher
        and "Start-Process -FilePath $GodotExe" in gui_launcher
        and "$child.HasExited" in gui_launcher
        and "godot_console_sha256=" in gui_launcher
        and "Verify-Pe64 $console" in package
        and "godot_console_companion_pe64_sha256" in package
        and "Extracted official Godot console companion missing or tampered" in tamper
        and "$cleanResult.godot_console_sha256 -cne $consoleOriginal" in tamper
    ),
    "windows-powershell5-real-dotnet-process-typed-exit-and-bounded-dual-pipes": (
        "New-Object System.Diagnostics.ProcessStartInfo" in gui_launcher
        and "New-Object System.Diagnostics.Process" in gui_launcher
        and "$child.StandardOutput.ReadToEndAsync()" in gui_launcher
        and "$child.StandardError.ReadToEndAsync()" in gui_launcher
        and "$child.WaitForExit(90000)" in gui_launcher
        and "$child.ExitCode" in gui_launcher
        and "$null -eq $exitValue" in gui_launcher
        and "$code=[int]$exitValue" in gui_launcher
        and "Wait-Process -Id $p.Id -Timeout 90" not in gui_launcher
        and "code=$code parser_error=$errors" in gui_launcher
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
