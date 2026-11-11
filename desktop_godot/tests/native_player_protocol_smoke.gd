extends SceneTree
## Headless protocol parser test; no native exe launched. End-to-end requires real Windows client.
const SESSION := preload("res://world/native_player_session.gd")
func _initialize() -> void:
	var s = SESSION.new()
	var got := []
	var statuses := []
	var ships := []
	var navs := []
	s.station_nav_received.connect(func(n: Dictionary): navs.append(n))
	s.ship_state_received.connect(func(f: Dictionary): ships.append(f))
	s.pose_received.connect(func(p: Dictionary): got.append(p))
	s.status_changed.connect(func(p: String): statuses.append(p))
	# Use native packet shape; synthetic frames here are NOT evidence of native custody.
	s._accept_line("POSE|1|123|0|0|0|0|0|0|1|2|0|0")
	s._accept_line("SHIP|1|123|0|0|0|2|0|1|0|0|0|0|0|0|0|1")
	s._accept_line("ROCKNONE|1|123|0|2|0|1|0|0|0")
	s._accept_line("NAV|1|123|0|2|0|1|77|2000|0|0")
	if got.size() != 1 or s._sequence != 0 or ships.size() != 1 or ships[0]["dock"] != 1 or \
			navs.size() != 1 or not navs[0]["found"] or int(navs[0]["station"]) != 77:
		push_error("FP3D02F: did not accept an initial native pose")
		quit(1)
		return
	s._pending = 1
	s._pending_kind = "MOVE"
	s._accept_line("ACK|1|123|1|1|80|0|0|1|2|2|0|0")
	if got.size() != 2 or got[1]["x_mm"] != 80 or s._sequence != 1:
		push_error("FP3D02F: did not update position from authorized acknowledgement")
		quit(1)
		return
	s._pending = 2
	s._pending_kind = "SEAT"
	s._accept_line("ACK|1|123|2|1|80|0|0|2|3|2|1|1")
	if got.size() != 3 or not got[2]["seated"] or s._sequence != 1 or s._seat_sequence != 1:
		push_error("FP3D03A: cockpit ACK failed to preserve walking pose and advance seat receipt")
		quit(1)
		return
	s._pending = 3
	s._pending_kind = "FLIGHT"
	s._accept_line("SHIP|1|123|3|1152921504606846979|0|2|3|4|250|0|0|250|0|0|345|0")
	s._accept_line("ROCKNONE|1|123|3|2|3|4|0|0|0")
	s._accept_line("NAV|1|123|3|2|3|4|77|-20000|0|0")
	if ships.size() != 2 or ships[1]["heading"] != 345 or ships[1]["vx"] != 250 or s._tick != 3 or \
			navs.size() != 2 or int(navs[1]["dx_mm"]) != -20000 or s._last_nav_request != 3:
		push_error("FP3D03B: original ship flight frame not accepted")
		quit(1)
		return
	s._pending = 4
	s._pending_kind = "FLIGHT"
	s._accept_line("DENY|1|4|native-flight-refused")
	if s._pending != 0 or s._sequence != 1 or not s._seated or statuses.is_empty():
		push_error("FP3D02F: rejected command changed native pose")
		quit(1)
		return
	print("FP3D03B Godot MOVE/SEAT/SHIP parser PASS; native executable runtime separately required")
	quit(0)
