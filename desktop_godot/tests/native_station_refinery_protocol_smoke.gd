extends SceneTree
## Synthetic frame/GUI contract only. Real original SQLite process is CTest.
const STATION := preload("res://world/native_station_session.gd")
const TERMINAL := preload("res://world/refinery_terminal_ui.gd")

func _initialize() -> void:
	call_deferred("_exercise")

func _exercise() -> void:
	var session = STATION.new()
	var states := []
	session.refinery_received.connect(func(state: Dictionary): states.append(state))
	session._accept_line("PERSON|2|123|0|0|2|4|1|0|2400|0|2|0")
	session._pending = 1
	session._accept_line("REFHEAD|3|123|1|0|2|1|1|2|1|1|2|4|1|0|1|1|0")
	session._accept_line("REFLOT|3|123|1|6|91|2|1|12345")
	session._accept_line("REFEND|3|123|1|0|2|1|0|0|0")
	if states.size() != 1 or int(states[0]["ore_tonnes"]) != 1 or int(states[0]["lots"][0]["id"]) != 6 or int(states[0]["mineral_total_kg"]) != 0 or session._pending != 0:
		push_error("03C synthetic original refinery lot frame was not admitted")
		quit(1)
		return
	session._pending = 2
	session._accept_line("REFHEAD|3|123|2|5|3|1|1|2|1|1|1|9|0|1000|0|0|5")
	for part in ["nickel_iron|860", "troilite|80", "olivine|30", "pyroxene|15", "other|15"]:
		session._accept_line("REFMIN|3|123|2|" + part)
	session._accept_line("REFEND|3|123|2|5|3|0|5|1|1000")
	if states.size() != 2 or int(states[1]["result"]) != 1 or int(states[1]["transferred_kg"]) != 1000 or int(states[1]["mineral_total_kg"]) != 1000 or session._generation != 3 or session._pending != 0:
		push_error("03C synthetic original SQLite refining ACK and 1000kg stock mismatch")
		quit(1)
		return

	# 04C: an independently validated one-tonne UNLOAD is not a 1000kg
	# refinery receipt. The lot is visible only after original source frames.
	session._pending = 3
	session._accept_line("REFHEAD|3|123|3|6|4|1|1|2|1|1|2|4|1|1000|1|1|5")
	session._accept_line("REFLOT|3|123|3|7|91|2|1|67890")
	for part in ["nickel_iron|860", "troilite|80", "olivine|30", "pyroxene|15", "other|15"]:
		session._accept_line("REFMIN|3|123|3|" + part)
	session._accept_line("REFEND|3|123|3|6|4|1|5|3|1")
	if states.size() != 3 or int(states[2]["result"]) != 3 or int(states[2]["unloaded_tonnes"]) != 1 or int(states[2]["transferred_kg"]) != 0 or session._generation != 4:
		push_error("04C synthetic one-tonne unload interpreted incorrectly or not admitted")
		quit(1)
		return
	# Fresh 03C and 04C terminal states must distinguish stock from ability
	# to unload; no optimistic mass is shown on a mere button press.
	var ui = TERMINAL.new()
	root.add_child(ui)
	await process_frame
	ui.present(states[0])
	if not ui.visible or ui._lots.item_count != 1 or int(ui._lots.get_item_metadata(0)) != 6 or not ui._refine_button.disabled:
		push_error("03C station UI selection or real cooldown guard failed")
		quit(1)
		return
	ui.present(states[2])
	if ui._lots.item_count != 1 or not ui._unload_button.disabled:
		push_error("04C station UI failed to observe already unloaded rock lot")
		quit(1)
		return
	ui.present(states[1])
	if ui._lots.item_count != 0 or not ui._refine_button.disabled:
		push_error("03C station UI displayed depleted physical ore as eligible")
		quit(1)
		return
	print("PLAY-STATION-03C/04C GODOT protocol+terminal smoke PASS (synthetic frames only; original world tested by native pipe CTest)")
	quit(0)
