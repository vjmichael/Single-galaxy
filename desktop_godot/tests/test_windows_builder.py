#!/usr/bin/env python3
"""Source-only invariant checks for the Windows Godot export gate; NOT native execution."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
builder = (root / "build_tools/Build-Windows.ps1").read_text(encoding="utf-8")
cmd = (root / "BUILD_WINDOWS.cmd").read_text(encoding="utf-8")
checks = {
    "release-pinned": "4.7.2-stable" in builder,
    "official-release-only": "api.github.com/repos/godotengine/godot-builds/releases/tags/" in builder,
    "requires-digest": "'^sha256:([0-9a-fA-F]{64})$'" in builder,
    "digest-match": "if ($actual -ne $hex)" in builder,
    "size-match": "[int64]$asset.size" in builder,
    "release-asset-origin": 'https://github.com/godotengine/godot-builds/releases/download/' in builder,
    "non-dotnet-editor": "Godot_v4.7.2-stable_win64.exe.zip" in builder,
    "official-export-template": "Godot_v4.7.2-stable_export_templates.tpz" in builder,
    "windows-only-template-extract": "'windows_release_x86_64.exe'" in builder,
    "headless-project-parser": "'--editor','--quit'" in builder,
    "headless-runtime-smoke": "'res://tests/godot_smoke.gd'" in builder,
    "release-export": "'--export-release','Windows Desktop'" in builder,
    "inspect-amd64-pe": "0x8664" in builder,
    "record-sha256": "state.executable_sha256" in builder,
    "godot-license": "GODOT_LICENSE.txt" in builder,
    "evidence-even-on-failure": "finally {" in builder and "build-result.json" in builder,
    "launch-after-explicit-flag": "if ($LaunchAfterExport)" in builder,
    "one-click-cmd": "powershell.exe -NoProfile -ExecutionPolicy Bypass" in cmd,
    "no-browsers": not any(x in builder for x in ("chromedriver", "websocket", "localhost:8000")),
    "no-skip-verification-switch": "SkipVerify" not in builder and "-SkipCertificateCheck" not in builder,
}
for name, ok in checks.items():
    assert ok, name
print(f"PASS {len(checks)}/{len(checks)} Windows build-gate source invariants; Windows Godot execution NOT tested")
