"""Station startup and Godot bridge review; not actual Godot engine execution."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
m = (root/'world/main.gd').read_text()
s = (root/'world/native_station_session.gd').read_text()
f = (root/'world/native_player_session.gd').read_text()
b = (root/'build_tools/Build-Windows.ps1').read_text()
assert 'KEY_H' in m and 'KEY_F8' not in m and 'KEY_F7' not in m
assert 'native_station_session.start()' in m
assert 'StationInterior_Prototype' in m and 'ShipInterior_Prototype' in m
assert 'func _show_native_location(in_station: bool)' in m
assert 'for body in station_space.get_children()' in m and 'for body in ship_space.get_children()' in m
assert 'body.collision_layer = 1 if in_station else 0' in m
assert 'body.collision_layer = 0 if in_station else 1' in m
assert '_show_native_location(last_station_player_state == 4)' in m
assert 'Process start alone is NOT proof of ship custody' in m
assert '_show_native_location(false)' not in m.split('func _on_native_player_connection(',1)[1].split('func _on_native_ship_intents(',1)[0]
assert '_show_native_location(false) # verified original owned ship pose' in m
assert '_show_native_location(true) # station first' in m
assert '_geometry_parent.add_child(hatch)' in m
# A station <-> cockpit handoff must not race the two native SQLite writers.
assert 'native_station_session.process_id()' in m
assert 'native_player_session.process_id()' in m
assert 'func _advance_native_handoff()' in m
assert 'OS.is_process_running(_handoff_pid)' in m
assert 'Native writer handoff aborted' in m
assert 'last_station_player_state = -1' in m
assert 'player.use_native_authority(true) # no unjournaled walking during handoff' in m
assert 'func process_id() -> int' in s and 'func process_id() -> int' in f
assert '_last_shutdown_pid' in s and '_last_shutdown_msec' in s
assert '_last_shutdown_pid' in f and '_last_shutdown_msec' in f
assert 'var exiting_pid: int = native_player_session.process_id()' in m
assert 'var exiting_pid: int = native_station_session.process_id()' in m
assert 'last_station_player_state != 0' in m and 'Board your existing ship first' in m
assert 'native_station_session.stop("Station writer closed before ship cockpit session")' in m
assert 'station_test_save_selected' in m and 'single_galaxy_station_test.sqlite' in m
assert 'native_station_session.submit_walk(heading)' in m
assert 'native_station_session.interact_hangar()' in m
assert 'player.global_position = Vector3(float(person["x_mm"]) / 1000.0' in m
assert 'func start(save_override: String = "")' in f
assert 'galaxy_desktop_station_host' in s and 'OS.execute_with_pipe' in s
assert '"--explicit-test-station"' in s and 'single_galaxy_station_test.sqlite' in s
assert 'NO AVAILABLE SHIP' in s and '"BOARD"' in s and '"EXIT"' in s
assert '_pending != 0' in s and 'tick <= _tick' in s
assert 'person_received.emit(_person.duplicate(true))' in s
assert not any(x in s for x in ['HTTPRequest','WebSocketPeer','http://','https://','sqlite3_open','sqlite3_exec'])
win=(root/'build_tools/Test-NativeStation.ps1').read_text()
assert 'exclusive-writer-denied' in win
assert 'station-crash-restart' in win
assert 'Competing native world writer was admitted' in win
assert 'native-station-protocol' in b and 'galaxy_desktop_station_host.exe' in b
print('PLAY-STATION-02 Godot source integrity PASS: native fixture, ACK-only custody, real boarding gate, separate writers')
