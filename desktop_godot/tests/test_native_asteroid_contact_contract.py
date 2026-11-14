#!/usr/bin/env python3
"""Original native asteroid source / no-client-mining invariant checker.
Source check only. Godot engine and actual SQLite child are separate gates.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
repo=root.parent
host=(repo/"src/desktop_player_host.cpp").read_text()
station=(repo/"src/desktop_station_host.cpp").read_text()
header=(repo/"include/galaxy/SpaceMiningContact.hpp").read_text()
session=(root/"world/native_player_session.gd").read_text()
scene=(root/"world/main.gd").read_text()
smoke=(root/"tests/native_asteroid_contact_smoke.gd").read_text()
checks={
"authentic physical source": "world.entity_ids_in_system" in header and "world.same_local_frame" in header,
"high-order 3D coordinates": "physical_galactic" in header and "nearby_axis_delta" in header,
"finite ore and owner check": "rock->ore<=0" in header and "ship->owner_id!=owner" in header,
"only in-space contact": "ship->dock_state!=DockState::InSpace" in header,
"bounded contact sensor": "kContactRangeMm=30000" in header and "kMaxSystemEntitiesScanned=8192" in header,
"requires installed mining tool": "kMiningToolHardpoint=4" in header and "world.hardpoint_effective_on" in header,
"no free fitting seeded": "fitting.hardpoints" not in station,
"real test-rock-only genesis": "Explicit test-station genesis ONLY" in station and "add_geological_asteroid" in station,
"follows original ship telemetry": "send_ship(session,req,command_id,*outcome" in host and "send_rock_contact(session,req,world" in host,
"godot identity and order enforcement": "_last_ship_telemetry_request" in session and "_last_contact_request" in session,
"godot no mineral mint": "Action::Mine" not in scene and "Action::Mine" not in session,
"godot real asteroid mesh": "OriginalAsteroid_VisualOnly" in scene and "SphereMesh.new()" in scene,
"no Godot collision authority": "no fake collision authority" in scene and "CollisionShape3D.new()" not in scene.split("func _build_native_space_preview",1)[1].split("func _show_native_location",1)[0],
"verified external view": "event.keycode == KEY_V" in scene and "_last_native_dock == 0" in scene,
"synthetic parser test": "ROCKNONE|1|" in smoke and "ROCK|1|" in smoke,
}
for key,valid in checks.items(): assert valid,key
print(f"PLAY-STATION-04A PASS {len(checks)}/{len(checks)} source contracts (NOT engine or native runtime)")
