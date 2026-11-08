#!/usr/bin/env python3
"""Check source-only archive and game-runtime separation, not a Godot runtime test."""
import sys
from pathlib import Path
root = Path(__file__).resolve().parents[1]
required = ["project.godot","export_presets.cfg","world/main.tscn","world/main.gd",
            "world/player_controller.gd","world/input_contract.gd","README.md",
            "BUILD_AND_TEST.md","tests/godot_smoke.gd"]
for file in required:
    if not (root/file).is_file():
        raise AssertionError(f"missing expected game source: {file}")
for file in root.rglob("*"):
    if file.is_file() and file.suffix.lower() in {".html",".js",".pyc",".png",".jpg",".jpeg",".webp"}:
        raise AssertionError(f"unexpected web/asset file {file}")
assert "Windows Desktop" in (root/"export_presets.cfg").read_text()
assert "binary_format/embed_pck=true" in (root/"export_presets.cfg").read_text()
print("PASS: standard Windows/Linux native export presets and asset-free source checks; no executable claimed")
