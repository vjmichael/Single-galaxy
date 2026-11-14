extends SceneTree
## INT-02 / PLAY-STATION-04B: recovered historical Godot mining parser coverage.
## Synthetic packet regression ONLY. Native C++ command execution, original
## SQLite resource custody, and exact-head OS acceptance remain separate.
const SESSION := preload("res://world/native_player_session.gd")

func _initialize() -> void:
	var session = SESSION.new()
	var contacts := []
	var navs := []
	var equipment := []
	session.asteroid_contact_received.connect(func(contact: Dictionary): contacts.append(contact))
	session.station_nav_received.connect(func(nav: Dictionary): navs.append(nav))
	session.equipment_result_received.connect(func(kind: String, request: int, outcome: int, command_id: int):
		equipment.append({"kind":kind,"request":request,"outcome":outcome,"command_id":command_id}))
	# The current Godot/native contract requires verified POSE/SHIP/ROCK/NAV
	# source identity and tick/generation pairing, not the old standalone ROCK.
	session._accept_line("POSE|1|321|0|0|0|0|0|10|2|7|1|1")
	session._accept_line("SHIP|1|321|0|0|0|7|10|2|0|0|0|0|0|0|0|0")
	session._accept_line("ROCK|1|321|0|7|10|2|11|3770|14000|0|0|10000|2|0|24|1|0|0")
	session._accept_line("NAVNONE|1|321|0|7|10|2")
	if contacts.size() != 1 or navs.size() != 1 or session._current_rock_id != 11 or \
			not session._drill_online or contacts[0]["ore_tonnes"] != 3770 or \
			contacts[0]["cargo_tonnes"] != 0 or contacts[0]["impact_rock"] != 0:
		push_error("04B historical initial mine-ready contact not accepted by current parser")
		quit(1)
		return
	# Command IDs and exact stock changes are synthetic packet values here;
	# only original host/native CTest can prove their actual physical origin.
	session._pending = 1
	session._pending_kind = "MINE"
	session._accept_line("SHIP|1|321|1|1152921504606846977|0|7|11|3|0|0|0|0|0|0|0|0")
	session._accept_line("ROCK|1|321|1|7|11|3|11|3769|14000|0|0|10000|2|1|24|1|0|0")
	session._accept_line("NAVNONE|1|321|1|7|11|3")
	if contacts.size() != 2 or navs.size() != 2 or contacts[1]["ore_tonnes"] != 3769 or \
			contacts[1]["cargo_tonnes"] != 1 or session._pending != 0 or \
			equipment.size() != 1 or equipment[0]["kind"] != "MINE" or \
			equipment[0]["request"] != 1 or equipment[0]["outcome"] != 0:
		push_error("04B synthetic one-tonne original-world mining acknowledgement regression")
		quit(1)
		return
	session._pending = 2
	session._pending_kind = "POWER"
	session._accept_line("SHIP|1|321|2|1152921504606846978|0|7|12|4|0|0|0|0|0|0|0|0")
	session._accept_line("ROCK|1|321|2|7|12|4|11|3769|14000|0|0|10000|2|1|24|0|0|0")
	session._accept_line("NAVNONE|1|321|2|7|12|4")
	if contacts.size() != 3 or navs.size() != 3 or session._drill_online or \
			contacts[2]["ore_tonnes"] != 3769 or contacts[2]["cargo_tonnes"] != 1 or \
			equipment.size() != 2 or equipment[1]["kind"] != "POWER" or \
			equipment[1]["request"] != 2 or equipment[1]["outcome"] != 0:
		push_error("04B synthetic fitted drill power-off or material conservation regression")
		quit(1)
		return
	print("INT-02 PLAY-STATION-04B Godot historical mining parser PASS (synthetic frames only)")
	quit(0)
