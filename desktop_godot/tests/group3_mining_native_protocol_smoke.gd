extends SceneTree
## GROUP3: native protocol parser smoke only. Native SQLite C++ acceptance
## runs separately; no client-side mining of synthetic game resources.
const NATIVE := preload("res://world/native_player_session.gd")
const PANEL := preload("res://world/group3_mining_panel.gd")
const MAIN := preload("res://world/main.gd")
func _initialize() -> void:
	call_deferred("_run")
func _fail(reason: String) -> void:
	push_error("GROUP3-GODOT-NATIVE FAIL: " + reason)
	quit(1)
func _run() -> void:
	var s = NATIVE.new()
	s._active = true
	s._session = 123
	s._ship = 2
	s._tick = 4
	s._generation = 9
	s._initial_ship_seen = true
	s._seated = true
	s._seat_sequence = 1
	s._last_ship_telemetry_request = 3
	s._last_contact_request = 3
	s._last_nav_request = 3
	s._pending_kind = "SURVEY"
	s._pending = 16
	var surveys: Array[Dictionary] = []
	var equipment: Array[Dictionary] = []
	var contacts: Array[Dictionary] = []
	s.asteroid_survey_received.connect(func(obs: Dictionary):surveys.append(obs))
	s.equipment_result_received.connect(func(kind: String,req: int,outcome: int,cmd: int):
		equipment.append({"kind":kind,"req":req,"outcome":outcome,"command":cmd}))
	s.asteroid_contact_received.connect(func(contact: Dictionary):contacts.append(contact))
	s._accept_line("SHEAD|1|123|16|2|4|9|1|0")
	s._accept_line("SROW|1|123|16|2|4|9|77|42000|0|0|10000|9|2|1")
	s._accept_line("SEND|1|123|16|2|4|9")
	if surveys.size()!=1 or s._pending!=0 or
			int(surveys[0]["sources"][0]["rock"])!=77 or
			int(surveys[0]["sources"][0]["ore_tonnes"])!=9:
		_fail("source-only original asteroid survey failed full native identity, redaction and finish")
		return
	s._pending_kind = "MINE"
	s._pending = 17
	s._last_send_msec = Time.get_ticks_msec()
	s._accept_line("SHIP|1|123|17|1152921504606847000|0|2|5|10|0|0|0|0|0|0|90|0")
	s._accept_line("ROCK|1|123|17|2|5|10|77|9|1000|0|0|2000|2|1|12|1|0|0")
	s._accept_line("NAV|1|123|17|2|5|10|1|0|0|0")
	if equipment.size()!=1 or equipment[0]["kind"]!="MINE" or
			equipment[0]["outcome"]!=0 or s._pending!=0 or
			contacts.size()!=1 or int(contacts[0]["cargo_tonnes"])!=1 or
			int(contacts[0]["ore_tonnes"])!=9 or not contacts[0]["mining_tool_online"]:
		_fail("exact original MINE SHIP/ROCK/NAV receipt not accepted")
		return
	var ui = PANEL.new()
	root.add_child(ui)
	if ui._mine==null or ui._survey_button==null:
		_fail("real interactive mining/fitting controls missing")
		return
	ui.show_original_ship({"ship":2,"tick":5,"gen":10,"dock":0}, contacts[0], true)
	if ui._mine.disabled:
		_fail("original powered tool and matching rock not presented in player UI")
		return
	ui.show_original_survey(surveys[0])
	if not ui._survey.text.contains("9t original ore"):
		_fail("Original fitted scanner mineral readback not shown in Godot UI")
		return
	var scene = MAIN.new()
	if not scene.has_method("_on_group3_asteroid_survey") or
			not scene.has_method("_on_group3_equipment_receipt"):
		_fail("real game lacks original mining receipt routing")
		return
	print("GROUP3 GODOT SOURCE PROTOCOL PASS: native survey batch ID/tick/gen, unknown geology, immutable mined SHIP/ROCK/NAV receipt and live UI; real C++ conservation tested independently")
	quit(0)
