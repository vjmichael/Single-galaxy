#!/usr/bin/env python3
"""FP3D-ENV-01G/H: bounded source parity only; not a substitute for native OS/GPU."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
repo = root.parent
cpp = (repo / "src/NativeInteriorMovement.cpp").read_text()
host = (repo / "src/desktop_player_host.cpp").read_text()
persist = (repo / "src/Persistence.cpp").read_text()
sim = (repo / "src/Simulation.cpp").read_text()
header = (repo / "include/galaxy/NativeDockingTarget.hpp").read_text()
geometry = (repo / "include/galaxy/NativeStationWalkGeometry.hpp").read_text()
refinery = (repo / "include/galaxy/StationRefineryProtocol.hpp").read_text()
scene = (root / "world/main.gd").read_text()
controller = (root / "world/player_controller.gd").read_text()
inputs = (root / "world/input_contract.gd").read_text()
native_test = (repo / "tests/FP3D03B_CockpitFlightTests.cpp").read_text()
world_test = (repo / "tests/FP3D_ENV01_SweptCollisionTests.cpp").read_text()
godot_test = (root / "tests/fp3d_env01_live_scene_acceptance.gd").read_text()
checks = {
    "native target rather than UI station ID":
        "nearest_original_station(world_.world(),*ship)" in cpp and
        "world.entity_ids_in_system" in header and "world.same_local_frame" in header,
    "true 3D docking range": "Simulation::kDockingRangeMm" in header and
        "nearby_axis_delta" in header,
    "same original world docking journal": "c.action=Action::RequestDock" in cpp and
        "world_.submit_cockpit_flight_checked(c,expected_seat_sequence)" in cpp,
    "seat and target authorizer": "command.action!=Action::RequestDock" in persist and
        "command.action==Action::RequestDock" in persist,
    "explicit OS pipe verb": 'name=="DCK"' in host,
    "native C++ dock+restart test": "pending-dock fail-closed" in native_test and
        "same owned ship can depart again after docking" in native_test,
    "original instrument preserved": "ReadOnlyOriginalPilotInstrument" in scene,
    "Godot G key only sends intent": '"ship_dock": KEY_G' in inputs and
        'pilot_actions.append("DCK")' in controller and
        'Action::RequestDock' not in scene,
    "new world station walk guarded": "snapshot_version_>=125" in sim and
        "station_walk::clear(" in sim,
    "native crate and wall matches Godot": "box(x,z,3000,0,450,450)" in geometry and
        '"Resource crate 02"' in scene and
        "box(x,z,-5000,-7000,200,14000)" in geometry and
        '"Hall west wall"' in scene,
    "ship interior source obstacle rendered":
        'Native interior cargo obstruction' in scene and
        "Vector3(1.6, 0.8, 0)" in scene,
    "native station walk boundary visual only":
        "NativeStationWalkEnvelope_16m_VisualOnly" in scene and
        "MeshInstance3D.new()" in scene,
    "refinery reachable beyond padded cargo collider":
        "kTerminalRadiusMm=1200" in refinery and
        'kTerminalXmm=3000' in refinery,
    "station movement SQLite recovery in group gate":
        "real_station_collision_test" in world_test and
        "station_player_action(" in world_test and
        "PersistentWorld recovered" in world_test,
    "real Godot test docks before second undock":
        '_submit_visible_cursor_keyboard_flight("ship_dock", "DCK")' in godot_test and
        '_submit_visible_cursor_keyboard_flight("ship_undock", "UND")' in godot_test and
        '_game.player.native_ship_intents.emit(intents)' in godot_test and
        "second original keyboard launch after docking failed" in godot_test,
    "no new client mining or resource creation":
        all(tok not in scene for tok in ["Action::Mine", "Action::UnloadOre"]) and
        "Fitting" not in geometry and
        "Action::Mine" not in cpp,
}
for label, success in checks.items():
    assert success,label
print("FP3D-ENV-01G/H PASS:", len(checks),
      "source-only invariants; installed C++/Godot/Windows runtime remains required")
