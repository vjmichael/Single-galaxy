extends SceneTree
## Headless protocol parser test; no native exe launched. End-to-end requires real Windows client.
const SESSION := preload("res://world/native_player_session.gd")
func _initialize() -> void:
	var s = SESSION.new()
	var got := []
	var statuses := []
	s.pose_received.connect(func(p: Dictionary): got.append(p))
	s.status_changed.connect(func(p: String): statuses.append(p))
	# Use native packet shape; synthetic frames here are NOT evidence of native custody.
	s._accept_line("POSE|1|123|0|0|0|0|0|0|1|2")
	if got.size() != 1 or s._sequence != 0:
		push_error("FP3D02F: did not accept an initial native pose")
		quit(1)
		return
	s._pending = 1
	s._accept_line("ACK|1|123|1|1|80|0|0|1|2|2")
	if got.size() != 2 or got[1]["x_mm"] != 80 or s._sequence != 1:
		push_error("FP3D02F: did not update position from authorized acknowledgement")
		quit(1)
		return
	s._pending = 2
	s._accept_line("DENY|1|2|rate-limit")
	if s._pending != 0 or s._sequence != 1 or statuses.is_empty():
		push_error("FP3D02F: rejected command changed native pose")
		quit(1)
		return
	print("FP3D02F Godot protocol parsing PASS; runtime native connection unverified")
	quit(0)
