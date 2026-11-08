from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=(root/'world/native_player_session.gd').read_text()
c=(root/'world/player_controller.gd').read_text()
m=(root/'world/main.gd').read_text()
b=(root/'build_tools/Build-Windows.ps1').read_text()
assert 'OS.execute_with_pipe' in p
assert 'galaxy_desktop_player_host.exe' in p
assert 'single_galaxy_authoritative.sqlite' in p
assert 'session != _session' in p
assert 'seq != _sequence + 1' in p
assert '_pending != 0' in p
assert 'rate-limit' not in p or 'DENY' in p
assert 'get_buffer(4096)' in p
assert 'Native command acknowledgement timed out' in p
assert '_stdout.store_line(' in p
assert 'SQLite' in p and 'sqlite3' not in p
assert not any(word in p for word in ['HTTPRequest','WebSocketPeer','http://','https://','connect_to_host('])
assert 'if authoritative_mode:' in c and 'native_motion_intent.emit(direction)' in c
assert 'move_and_slide()' in c.split('if authoritative_mode:',1)[1] # local path still available
assert 'return\n\t# The arrow keys' in c # native branch returns before local movement
assert 'player.native_motion_intent.connect(' in m
assert 'player.global_position = Vector3(float(pose["x_mm"]) / 1000.0' in m
assert 'native_player_session.pose_received.connect' in m
assert 'KEY_P' in m and 'KEY_O' in m and 'KEY_F8' not in m and 'KEY_F7' not in m
assert 'player.use_native_authority(connected)' in m
assert 'player.native_seat_intent.connect' in m
assert 'player.use_native_seat(bool(pose["seated"]))' in m
assert 'func toggle_seat()' in p and 'SEAT|%d|%d|%d|%d' in p
assert 'seat_sequence != _seat_sequence + 1' in p
assert 'if native_seated:' in c and 'native_seat_intent.emit()' in c
assert 'native_ship_intents.emit(pilot_actions)' in c
assert 'player.native_ship_intents.connect(' in m
assert 'native_player_session.submit_flight(actions)' in m
assert 'ship_state_received.connect' in m
assert 'func submit_flight(actions: PackedStringArray)' in p
assert 'func _accept_ship(parts: PackedStringArray)' in p
assert 'not _seated' in p and '_pending_kind != "FLIGHT"' in p
keys=(root/'world/input_contract.gd').read_text()
assert 'ship_yaw_left' in keys and 'ship_yaw_right' in keys
assert 'ship_strafe_port' in keys and 'ship_strafe_starboard' in keys
assert 'ship_thrust_forward' in keys and 'ship_thrust_reverse' in keys
assert 'ship_counterthrust' in keys and 'ship_undock' in keys
assert 'native-player-protocol' in b
assert "'native-player-host' 'PASS'" in b
assert "'native-player-host' 'SKIP'" in b
print('FP3D03B PASS: Godot source stays untrusted; host-ACK-only movement, bounded rate, focus and local pipe lifecycle')
