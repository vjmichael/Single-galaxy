extends SceneTree
## Synthetic parser shape only. Native process CTest tests real custody.
const SESSION := preload("res://world/native_station_session.gd")
func _initialize() -> void:
	var s = SESSION.new()
	var got := []
	var statuses := []
	s.person_received.connect(func(p: Dictionary): got.append(p))
	s.status_changed.connect(func(message: String): statuses.append(message))
	s._accept_line("PERSON|2|123|0|0|2|4|1|0|1600|0|2|0")
	if got.size() != 1 or s._generation != 2 or got[0]["state"] != 4 or got[0]["eligible_ship"] != 2:
		push_error("PLAY-STATION-02: initial original station person failed protocol validation")
		quit(1)
		return
	s._pending = 1
	s._accept_line("PERSON|2|123|1|1|3|4|1|0|1500|0|2|101")
	if got.size() != 2 or got[1]["x_mm"] != 1500 or s._pending != 0:
		push_error("PLAY-STATION-02: walking ACK not admitted")
		quit(1)
		return
	s._pending = 2
	s._accept_line("DENY|2|2|station-custody-or-distance")
	if got.size() != 2 or s._pending != 0 or statuses.is_empty():
		push_error("PLAY-STATION-02: rejected boarding moved the player")
		quit(1)
		return
	s._pending = 3
	s._accept_line("PERSON|2|123|3|2|4|0|1|2|0|0|2|103")
	if got.size() != 3 or got[2]["state"] != 0 or got[2]["ship"] != 2 or got[2]["x_mm"] != 0:
		push_error("PLAY-STATION-02: original boarding ACK not admitted")
		quit(1)
		return
	print("PLAY-STATION-02 Godot PERSON/WALK/BOARD/DENY parser PASS (synthetic; native runtime tested separately)")
	quit(0)
