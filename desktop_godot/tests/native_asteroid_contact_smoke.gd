extends SceneTree
## Synthetic native-frame parser acceptance only, not an actual Windows flight.
const SESSION := preload("res://world/native_player_session.gd")
func _initialize() -> void:
	var s = SESSION.new()
	var contacts := []
	s.asteroid_contact_received.connect(func(contact: Dictionary): contacts.append(contact))
	s._accept_line("POSE|1|321|0|0|0|0|0|10|2|7|1|1")
	s._accept_line("SHIP|1|321|0|0|0|7|10|2|0|0|0|0|0|0|0|0")
	s._accept_line("ROCK|1|321|0|7|10|2|11|167|12500|0|0|2000|2|0|24|0")
	if contacts.size() != 1 or not contacts[0]["found"] or contacts[0]["rock"] != 11 or contacts[0]["ore_tonnes"] != 167 or contacts[0]["mining_tool_online"] != 0:
		push_error("04A native asteroid contact not accepted from exact source frame")
		quit(1)
		return
	s._pending = 1
	s._pending_kind = "FLIGHT"
	s._accept_line("SHIP|1|321|1|1152921504606846977|0|7|11|3|250|0|0|250|0|0|0|0")
	s._accept_line("ROCKNONE|1|321|1|7|11|3|0|24|0")
	if contacts.size() != 2 or contacts[1]["found"] or contacts[1]["cargo_tonnes"] != 0 or s._pending != 0:
		push_error("04A source absence frame or confirmed ship tick not accepted")
		quit(1)
		return
	print("PLAY-STATION-04A Godot contact parser PASS (synthetic; real native physical contact independently CTested)")
	quit(0)
