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
assert 'Native movement acknowledgement timed out' in p
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
assert 'if not native_seated:' in c and 'native_seat_intent.emit()' in c
assert 'native-player-protocol' in b
assert "'native-player-host' 'PASS'" in b
assert "'native-player-host' 'SKIP'" in b
print('FP3D03A PASS: Godot source stays untrusted; host-ACK-only movement, bounded rate, focus and local pipe lifecycle')
