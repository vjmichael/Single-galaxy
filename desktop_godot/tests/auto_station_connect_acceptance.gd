extends SceneTree
## FP3D-UX-03: REAL original C++ station/SQLite/Godot startup. This is a
## hosted CI-only user-data sandbox test; never run on a player's world.
## Proven original PERSON, WALK, BOARD and automatic cockpit POSE, including
## recovering the same save without H/P or a second SQLite writer.

var game: Node3D
var _error := ""

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

func _walk_ack(generation: int) -> bool:
	return not is_instance_valid(game) or not game.native_station_session.is_active() or \
		(game.native_station_session._pending == 0 and \
		int(game.native_station_session._generation) > generation)

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
	if not await _wait_for(_station_ready):
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
	var person := game.native_station_session._person
	if int(person.get("x_mm", -1)) != 1600:
		_fail("original genesis no longer starts at 1600mm (world may be foreign)")
		quit(1)
		return
	for index in range(12):
		var native = game.native_station_session
		var generation: int = int(native._generation)
		await create_timer(0.14).timeout
		native.submit_walk(Vector3.LEFT)
		if not await _wait_for(func() -> bool: return _walk_ack(generation), 4.0):
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
	if not await _wait_for(_ship_ready, 16.0):
		_fail("E BOARD did not automatically hand off saved SQLite to cockpit: " + str(game.native_label.text))
		quit(1)
		return
	if game.native_station_session.is_active() or not game.station_test_save_selected or \
		game.last_station_player_state != 0:
		_fail("ship ready without an original BOARD receipt or second writer still active")
		quit(1)
		return
	var old_pid: int = game.native_player_session.process_id()
	game.queue_free()
	await process_frame
	if not await _wait_for(func() -> bool: return not OS.is_process_running(old_pid), 8.0):
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
	if not await _wait_for(_ship_ready, 20.0):
		_fail("saved boarded ship did not reopen automatically without H/P: " + str(game.native_label.text))
		quit(1)
		return
	if game.native_station_session.is_active() or game.last_station_player_state != 0:
		_fail("restart did not preserve original aboard custody")
		quit(1)
		return
	game.queue_free()
	await process_frame
	print("FP3D-UX-03 REAL GODOT AUTO-STATION PASS: no H/P, original C++ PERSON, 12 admitted WALK, E BOARD, supervised single-writer cockpit, same SQLite saved-boarded restart")
	quit(0)
