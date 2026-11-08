#!/usr/bin/env python3
"""Dependency-free source contract checks. Not equivalent to running Godot."""
from pathlib import Path
p=Path(__file__).resolve().parents[1]
project=(p/'project.godot').read_text()
player=(p/'world/player_controller.gd').read_text()
scene=(p/'world/main.gd').read_text()
keys=(p/'world/input_contract.gd').read_text()
assert 'main_scene="res://world/main.tscn"' in project
assert 'gl_compatibility' in project
assert (p/'tests/godot_smoke.gd').is_file()
assert 'extends CharacterBody3D' in player
assert 'move_and_slide()' in player
assert 'InputEventMouseMotion' in player
assert 'Input.is_action_just_pressed("fps_inspect")' in player
assert 'KEY_W' in keys and 'KEY_S' in keys and 'KEY_A' in keys and 'KEY_D' in keys
assert 'KEY_UP' in keys and 'KEY_DOWN' in keys
assert '"fps_forward": KEY_W' in keys
assert '"fps_forward": KEY_UP' not in keys
assert '"fps_backward": KEY_DOWN' not in keys
assert 'Input.get_vector("fps_left", "fps_right", "fps_forward", "fps_backward")' in player
assert 'Input.mouse_mode = Input.MOUSE_MODE_VISIBLE' in player
assert 'collision.shape = shape' in scene
assert 'CollisionShape3D.new()' in player
assert 'LOCAL MOVEMENT TEST ONLY' in scene
assert 'WebGL' not in scene and '127.0.0.1' not in scene
assert not any(x in player+scene+keys for x in ['WebSocketPeer','HTTPClient','HTTPRequest','WebSocketMultiplayerPeer','OS.execute','create_process'])
assert not list(p.rglob('*.png')) and not list(p.rglob('*.jpg'))
print('PASS 20/20 source-level project and separation assertions; Godot runtime NOT executed')
