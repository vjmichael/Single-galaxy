extends SceneTree
## Synthetic Godot parser acceptance only. Actual C++ child separately tested.
const SESSION := preload("res://world/native_player_session.gd")
func _initialize() -> void:
	var s = SESSION.new()
	var contacts := []
	s.asteroid_contact_received.connect(func(contact: Dictionary): contacts.append(contact))
	s._accept_line("POSE|1|321|0|0|0|0|0|10|2|7|1|1")
	s._accept_line("SHIP|1|321|0|0|0|7|10|2|0|0|0|0|0|0|0|0")
	s._accept_line("ROCK|1|321|0|7|10|2|11|3770|14000|0|0|10000|2|0|24|1")
	if s._current_rock_id != 11 or not s._mining_tool_online:
		push_error("04B original rock/contact/power not selected")
		quit(1)
		return
	s._pending = 1
	s._pending_kind = "MINE"
	s._accept_line("SHIP|1|321|1|1152921504606846977|0|7|11|3|0|0|0|0|0|0|0|0")
	s._accept_line("ROCK|1|321|1|7|11|3|11|3769|14000|0|0|10000|2|1|24|1")
	if contacts.size()!=2 or contacts[1]["ore_tonnes"]!=3769 or contacts[1]["cargo_tonnes"]!=1 or s._pending!=0:
		push_error("04B exact one-tonne original mining receipt mismatch")
		quit(1)
		return
	s._pending = 2
	s._pending_kind = "POWER"
	s._accept_line("SHIP|1|321|2|1152921504606846978|0|7|12|4|0|0|0|0|0|0|0|0")
	s._accept_line("ROCK|1|321|2|7|12|4|11|3769|14000|0|0|10000|2|1|24|0")
	if contacts.size()!=3 or s._mining_tool_online or contacts[2]["ore_tonnes"]!=3769:
		push_error("04B powered-off mining tool frame mismatch")
		quit(1)
		return
	print("PLAY-STATION-04B Godot original ship/mining receipt parser PASS (synthetic)")
	quit(0)
