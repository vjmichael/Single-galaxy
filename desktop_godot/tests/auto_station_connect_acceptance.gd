extends SceneTree
## FP3D-UX-03: REAL original C++ station/SQLite/Godot startup. This is a
## hosted CI-only user-data sandbox test; never run on a player's world.
## Proven original PERSON, WALK, BOARD and automatic cockpit POSE, including
## recovering the same save without H/P or a second SQLite writer.

var game: Node3D
var _error := ""
var _walk_generation := 0
var _former_pid := -1

func _initialize() -> void:
	call_deferred("_run")

func _fail(message: String) -> void:
	_error = message
	push_error("FP3D-UX-03 real auto-start acceptance FAIL: " + message)

func _wait_for(check: Callable, seconds: float = 12.0) -> bool:
	var started: int = Time.get_ticks_msec()
	while Time.get_ticks_msec() - started < int(seconds * 1000.0):
		if check.call():
			return true
		await create_timer(0.035).timeout
	return false

func _station_ready() -> bool:
	return is_instance_valid(game) and game.native_station_session.is_active() and \
		int(game.native_station_session._session) > 0 and \
		int(game.native_station_session._person.get("state", -1)) == 4

func _ship_ready() -> bool:
	return is_instance_valid(game) and game.native_player_session.is_active() and \
		int(game.native_player_session._session) > 0 and \
		int(game.native_player_session._sequence) >= 0 and \
		bool(game.native_player_session._initial_ship_seen)

func _seat_ready() -> bool:
	return _ship_ready() and game.native_player_session._pending == 0 and game.native_player_session._seated

func _third_person_locked() -> bool:
	return _ship_ready() and game.space_preview.visible and \
		game.exterior_camera.current and not game.player.camera.current and \
		not game.ship_space.visible and game.player.native_in_ship

func _unchanged_pilot_seat(before_sequence: int) -> bool:
	return _ship_ready() and game.native_player_session._seated and \
		int(game.native_player_session._seat_sequence) == before_sequence

func _undocked() -> bool:
	return _ship_ready() and game.native_player_session._pending == 0 and game.native_player_session._dock_state == 0

func _redocked() -> bool:
	return _ship_ready() and game.native_player_session._pending == 0 and game.native_player_session._dock_state == 1 and game._colony_return_armed

func _colony_returned() -> bool:
	return _station_ready() and not game.native_player_session.is_active() and not game._colony_return_pending

func _walk_ack() -> bool:
	return not is_instance_valid(game) or not game.native_station_session.is_active() or \
		(game.native_station_session._pending == 0 and \
		int(game.native_station_session._generation) > _walk_generation)

func _old_writer_closed() -> bool:
	return _former_pid > 0 and not OS.is_process_running(_former_pid)

func _make_scene() -> void:
	var scene := load("res://world/main.tscn") as PackedScene
	if scene == null:
		_fail("Godot main.tscn unavailable")
		return
	game = scene.instantiate()
	root.add_child(game)

func _run() -> void:
	if OS.get_environment("SG_FP3D_CI_AUTOSTART") != "1":
		_fail("refusing to touch ordinary user save outside dedicated CI sandbox")
		quit(1)
		return
	var world_path := OS.get_user_data_dir().path_join("single_galaxy_station_test.sqlite")
	if FileAccess.file_exists(world_path):
		_fail("CI user data sandbox was not empty; refusing to overwrite an existing SQLite world")
		quit(1)
		return
	_make_scene()
	if not _error.is_empty():
		quit(1)
		return
	await process_frame
	if game._station_connect_attempted or game.native_station_session.is_active():
		_fail("script mode unexpectedly started or double-opened the writer")
		quit(1)
		return
	# Reproduce standalone game auto-connect explicitly under a script runner.
	game._player_session_autoconnect = true
	game._open_saved_station_automatically()
	if not (await _wait_for(Callable(self, "_station_ready"))):
		_fail("automatic original PERSON missing: " + str(game.native_label.text))
		quit(1)
		return
	if not FileAccess.file_exists(world_path) or not game.player.authoritative_mode:
		_fail("no actual original SQLite world or native custody after automatic launch")
		quit(1)
		return
	var original_session: int = int(game.native_station_session._session)
	game._retry_original_station()
	if not game.native_station_session.is_active() or int(game.native_station_session._session) != original_session:
		_fail("H troubleshooting key unexpectedly stopped a healthy native station writer")
		quit(1)
		return
	# Native genesis x=1600mm/z=0. Exactly 12 authorized -90mm moves
	# cross the original C++ hatch square without teleporting a client avatar.
	var person: Dictionary = game.native_station_session._person
	if int(person.get("x_mm", -1)) != 1600:
		_fail("original genesis no longer starts at 1600mm (world may be foreign)")
		quit(1)
		return
	for index in range(12):
		var native = game.native_station_session
		_walk_generation = int(native._generation)
		await create_timer(0.14).timeout
		native.submit_walk(Vector3.LEFT)
		if not (await _wait_for(Callable(self, "_walk_ack"), 4.0)):
			_fail("original station WALK did not acknowledge step " + str(index))
			quit(1)
			return
		if not native.is_active() or int(native._person.get("x_mm", 99999)) > 1600 - (index + 1) * 90:
			_fail("native C++ PERSON did not advance after step " + str(index))
			quit(1)
			return
	var final_x: int = int(game.native_station_session._person["x_mm"])
	if absi(final_x) > 600:
		_fail("authorized player has not reached native hatch: x=" + str(final_x))
		quit(1)
		return
	game.native_station_session.interact_hangar()
	if not (await _wait_for(Callable(self, "_ship_ready"), 16.0)):
		_fail("E BOARD did not automatically hand off saved SQLite to cockpit: " + str(game.native_label.text))
		quit(1)
		return
	if game.native_station_session.is_active() or not game.station_test_save_selected or \
		game.last_station_player_state != 0:
		_fail("ship ready without an original BOARD receipt or second writer still active")
		quit(1)
		return
	# A BOARD now authorizes pilot-only automatic C++ SEAT, not a walkable
	# cabin or another explicit E. Third-person applies even before U.
	if not (await _wait_for(Callable(self, "_seat_ready"), 8.0)):
		_fail("native automatic pilot seating was not committed after BOARD")
		quit(1)
		return
	if not _third_person_locked():
		_fail("BOARD did not immediately lock third-person/no-cabin state")
		quit(1)
		return
	var first_seat_seq: int = int(game.native_player_session._seat_sequence)
	game._on_native_player_seat_intent() # E must NOT stand even while docked
	await create_timer(0.15).timeout
	if not _unchanged_pilot_seat(first_seat_seq):
		_fail("E escaped the locked pilot seat while docked")
		quit(1)
		return
	_former_pid = game.native_player_session.process_id()
	game.queue_free()
	await process_frame
	if not (await _wait_for(Callable(self, "_old_writer_closed"), 8.0)):
		_fail("original cockpit writer survived forced game close; cannot safely restart")
		quit(1)
		return
	# Same original SQLite file on fresh game start. The registered player is
	# already aboard; auto station probes PERSON and auto reopens the cockpit.
	_make_scene()
	if not _error.is_empty():
		quit(1)
		return
	await process_frame
	game._player_session_autoconnect = true
	game._open_saved_station_automatically()
	if not (await _wait_for(Callable(self, "_ship_ready"), 20.0)):
		_fail("saved boarded ship did not reopen automatically without H/P: " + str(game.native_label.text))
		quit(1)
		return
	if game.native_station_session.is_active() or game.last_station_player_state != 0:
		_fail("restart did not preserve original aboard custody")
		quit(1)
		return
	# Saved BOARD remains a persisted original C++ automatic SEAT after
	# restart. No cockpit E to sit, no walkable cabin or free-look V toggle.
	if not (await _wait_for(Callable(self, "_seat_ready"), 8.0)) or not _third_person_locked():
		_fail("restart did not preserve automatic pilot-seat/third-person rule")
		quit(1)
		return
	await create_timer(0.14).timeout
	game.native_player_session.submit_flight(PackedStringArray(["UND"]))
	if not (await _wait_for(Callable(self, "_undocked"), 7.0)):
		_fail("original ship failed real undocking after restart")
		quit(1)
		return
	var flying_seat_sequence: int = int(game.native_player_session._seat_sequence)
	game._on_native_player_seat_intent() # E cannot stand/leave ship in space
	await create_timer(0.15).timeout
	if not _unchanged_pilot_seat(flying_seat_sequence) or not _third_person_locked():
		_fail("E or third-person camera rule allowed ship exploration in space")
		quit(1)
		return
	await create_timer(0.14).timeout
	game.native_player_session.submit_flight(PackedStringArray(["DCK"]))
	if not (await _wait_for(Callable(self, "_redocked"), 8.0)):
		_fail("original redocking did not arm real colony disembark")
		quit(1)
		return
	if not _third_person_locked() or not game.native_player_session._seated:
		_fail("real redocking exposed a walkable cabin or released the pilot seat")
		quit(1)
		return
	await create_timer(0.14).timeout
	game._on_native_player_seat_intent() # ONE E: seat receipt + station EXIT
	if not (await _wait_for(Callable(self, "_colony_returned"), 20.0)):
		_fail("native EXIT failed to return same SQLite person to colony: " + str(game.native_label.text))
		quit(1)
		return
	if game.last_station_player_state != 4 or game._colony_return_armed or \
			game.station_space == null or not game.station_space.visible or \
			game.space_preview.visible or game.player.native_in_ship or \
			not game.player.camera.current:
		_fail("return had no verified on-foot colony person and visible station")
		quit(1)
		return
	game.queue_free()
	await process_frame
	print("FP3D-PILOT-01 REAL GODOT PASS: BOARD auto-seat, fixed third-person, no cabin walking, E blocked in space, ONE E docked EXIT, original SQLite one writer")
	print("FP3D-UX-03 REAL GODOT AUTO-STATION PASS: no H/P, original C++ WALK/E BOARD, cockpit restart, auto-seat/undock/redock/one-E EXIT, same SQLite single writer")
	quit(0)
