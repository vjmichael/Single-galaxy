#!/usr/bin/env python3
"""FP3D-ENV-01F source-only contract; real Godot and original C++ are separate."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
instruments = (root / "world/native_flight_instruments.gd").read_text()
game = (root / "world/main.gd").read_text()
native = (root / "world/native_player_session.gd").read_text()
smoke = (root / "tests/fp3d_env01f_pilot_instruments_smoke.gd").read_text()
real = (root / "tests/fp3d_env01_live_scene_acceptance.gd").read_text()

checks = {
    "pure read-only target projection": "extends RefCounted" in instruments
        and "from_verified_frames(ship: Dictionary, rock: Dictionary)" in instruments,
    "matches original ship identity": 'int(rock.get("ship", -2)) != int(ship["ship"])' in instruments,
    "matches original tick": 'int(rock.get("tick", -2)) != int(ship.get("tick", -1))' in instruments,
    "matches original SQLite generation": 'int(rock.get("generation", -2)) != int(ship.get("gen", -1))' in instruments,
    "requires native in-space dock state": 'int(ship.get("dock", -1)) != 0' in instruments,
    "5.2m whole-hull collision proxy": "SHIP_PROXY_RADIUS_MM := 5200.0" in instruments
        and "CONTACT_SKIN_MM := 1.0" in instruments,
    "heading and relative bearing": "atan2(target_delta.y, target_delta.x)" in instruments
        and "bearing_deg" in instruments,
    "radial velocity signed": "radial_rate_mm_tick" in instruments
        and '"RECEDING"' in instruments and '"CLOSING"' in instruments,
    "no fabricated contact": '"impact_rock"' in instruments and '"impact_tick"' in instruments,
    "fresh SHIP invalidates older ROCK": "_verified_flight_ship = ship.duplicate(true)\n\t_verified_flight_rock.clear()" in game,
    "ROCK joins verified SHIP": "_verified_flight_rock = contact.duplicate(true)" in game
        and "FLIGHT_INSTRUMENTS.from_verified_frames(" in game,
    "disconnect invalidates displayed target": "_verified_flight_ship.clear()" in game
        and "_verified_flight_rock.clear()" in game
        and "FLIGHT SENSOR: offline" in game,
    "only verified session emits rock frames": 'if native_player_session == null or not native_player_session.is_active():' in game,
    "HUD is read-only projection": 'flight_instrument_label.name = "ReadOnlyOriginalPilotInstrument"' in game,
    "synthetic Godot engine tests exist": "extends SceneTree" in smoke
        and "stale ROCK must not join current SHIP" in smoke
        and "other owner's ship must never produce a target instrument" in smoke
        and "no space target from a docked C++ ship" in smoke,
    "real Godot keyboard to original C++ collision test integrated":
        '_submit_visible_cursor_keyboard_flight("ship_thrust_forward", "FWD")' in real
        and '_game.player.native_ship_intents.emit(intents)' in real
        and '"FLIGHT"' in real
        and "read-only pilot instrument did not join real original impact receipt" in real,
    "no new client-side motion or ore authority": all(v not in instruments for v in
        ("Simulation", "Action::Mine", "OS.execute", "store_line", "sqlite", "submit_flight")),
    "wire format unchanged": "asteroid_contact_received.emit(" in native
        and '"generation":generation' in native,
}
for key, value in checks.items():
    assert value, key
print(f"FP3D-ENV-01F PASS: {len(checks)} source-only pilot instrumentation contracts; native/Godot execution gates remain open")
