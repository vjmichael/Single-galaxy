#!/usr/bin/env python3
"""Guard original Godot source against the exact mixed-indentation preload failure.

This is a *source* check, not a Godot parser or runtime acceptance gate.
Run Godot 4.7.2 --headless --editor --quit and each scene smoke separately.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
scripts = sorted(root.rglob("*.gd"))
assert scripts, "Godot script inventory empty"
for path in scripts:
    source = path.read_text(encoding="utf-8")
    kinds = set()
    for number, line in enumerate(source.splitlines(), start=1):
        match = re.match(r"^([\t ]+)", line)
        if not match or not line.strip():
            continue
        indent = match.group(1)
        if "\t" in indent and " " in indent:
            raise AssertionError(f"{path.relative_to(root)}:{number}: mixed indentation on same line")
        kinds.add("tabs" if indent[0] == "\t" else "spaces")
        if indent[0] == " " and len(indent) % 4:
            raise AssertionError(f"{path.relative_to(root)}:{number}: non-4-space indentation")
    if len(kinds) > 1:
        raise AssertionError(f"{path.relative_to(root)}: used tab and space indentation in the same GDScript")
    for dependency in re.findall(r'preload\("res://([^"]+\.gd)"\)', source):
        assert (root / dependency).is_file(), f"{path.relative_to(root)}: missing preload {dependency}"
print(f"PASS: {len(scripts)} Godot scripts use consistent indentation and resolve local preload paths")
