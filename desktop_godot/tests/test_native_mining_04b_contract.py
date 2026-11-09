#!/usr/bin/env python3
"""Static-only contract checks; real OS/engine gates are mandatory."""
from pathlib import Path
r=Path(__file__).resolve().parents[2]
kernel=(r/"src/Simulation.cpp").read_text()
model=(r/"include/galaxy/Simulation.hpp").read_text()
host=(r/"src/desktop_player_host.cpp").read_text()
scene=(r/"desktop_godot/world/main.gd").read_text()
session=(r/"desktop_godot/world/native_player_session.gd").read_text()
persist=(r/"src/Persistence.cpp").read_text()
checks={
 "physical fitted module": "bool mining_tool()" in model,
 "two tonne material debit": "--colony->second.modules" in kernel,
 "one-tonne command rule": "snapshot_version_>=124 && c.amount!=1" in kernel,
 "physical geology provenance": "track_station_ore_mining" in kernel,
 "effective module power": "hardpoint_effective_on(actor.id" in kernel,
 "snapshot version": "galaxy-kernel-v124" in kernel,
 "journal decoder": "Action::ConfigureMiningTool" in persist,
 "host owns original mutation": "world.submit(c)" in host and "world.step()" in host,
 "mining event exact tonne": "event->transferred!=1" in host,
 "seat and owner checked": "!previous.seated" in host and "owner_id!=kLocalOwner" in host,
 "Godot cockpit mining": "KEY_M" in scene and "submit_mining()" in scene,
 "no Godot-side ore writes": "rock->ore -=" not in session and "Action::Mine" not in scene,
}
for name,valid in checks.items(): assert valid,name
print(f"PLAY-STATION-04B {len(checks)} source-only contracts PASS; no native acceptance implied")
