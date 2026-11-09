extends SceneTree
## FP3D-ENV-01E: real C++ child process + SQLite + Godot scene in ONE
## Windows/Linux execution. No fabricated ROCK/SHIP frames are injected.
## This checks scene graph, camera, model and native contact; headless CI
## cannot establish human-visible rendered pixels or driver/GPU acceptance.
const MAIN := preload("res://world/main.gd")
const NATIVE_SHIP := preload("res://world/native_player_session.gd")
const COLLISION_RADIUS_M := 15.201 # 10m original rock + 5.2m greybox ship + 1mm skin.
var _game
var _ship_frames: Array[Dictionary] = []
var _rock_frames: Array[Dictionary] = []
var _error := ""

func _initialize() -> void:
	call_deferred("_run")

func _fail(reason: String) -> void:
	_error = reason
	push_error("FP3D-ENV-01E live scene: " + reason)

func _await_condition(predicate: Callable, timeout_ms: int = 9000) -> bool:
	var start := Time.get_ticks_msec()
	while Time.get_ticks_msec() - start < timeout_ms:
		if predicate.call():
			return true
		await create_timer(0.04).timeout
	return false

func _poll_native() -> bool:
	if not is_instance_valid(_game):
		return false
	if _game.native_player_session == null:
		return false
	_game.native_player_session.poll()
	return true

func _native_ready() -> bool:
	_poll_native()
	var n = _game.native_player_session
	return n._active and n._session > 0 and n._initial_ship_seen and n._last_contact_request == 0

func _seat_ready() -> bool:
	_poll_native()
	var n = _game.native_player_session
	return n._active and n._pending == 0 and n._seated and n._seat_sequence == 1

func _undocked() -> bool:
	_poll_native()
	var n = _game.native_player_session
	return n._active and n._pending == 0 and n._dock_state == 0

func _docked() -> bool:
	_poll_native()
	var n = _game.native_player_session
	return n._active and n._pending == 0 and n._dock_state == 1

func _flight_done() -> bool:
	_poll_native()
	var n = _game.native_player_session
	return not n._active or n._pending == 0

func _on_ship(frame: Dictionary) -> void:
	_ship_frames.append(frame.duplicate(true))

func _on_rock(frame: Dictionary) -> void:
	_rock_frames.append(frame.duplicate(true))

func _run() -> void:
	var path := OS.get_environment("SG_FP3D_E2E_SAVE")
	if path.is_empty() or not FileAccess.file_exists(path):
		_fail("explicit original SQLite fixture missing")
		quit(1)
		return
	_game = MAIN.new()
	root.add_child(_game)
	await process_frame
	if not is_instance_valid(_game.player) or not is_instance_valid(_game.exterior_ship_visual) or \
			not is_instance_valid(_game.exterior_camera) or not is_instance_valid(_game.native_asteroid_mesh):
		_fail("real 3D ship / camera / asteroid scene nodes absent")
		quit(1)
		return
	if _game.exterior_ship_visual.get_child_count() != 4:
		_fail("expected 8m hull, two wings and mounted third-person camera")
		quit(1)
		return
	var session = _game.native_player_session
	session.ship_state_received.connect(_on_ship)
	session.asteroid_contact_received.connect(_on_rock)
	session.start(path)
	if not (await _await_condition(Callable(self, "_native_ready"))):
		_fail("original C++ host did not deliver authenticated initial POSE, SHIP and ROCK")
	elif _rock_frames.is_empty() or _rock_frames[-1]["impact_rock"] != 0:
		_fail("initial fixture forged or replayed original collision")
	else:
		# Exercise the actual Godot admission path with a live original SGDX
		# guardian whose saved stop timestamp is deliberately over 3s old.
		# A second same-role writer must NOT even be launched.
		var live_guardian: int = session.process_id()
		var attempted_overlap = NATIVE_SHIP.new()
		attempted_overlap._last_shutdown_pid = live_guardian
		attempted_overlap._last_shutdown_msec = Time.get_ticks_msec() - 10000
		if live_guardian <= 0 or attempted_overlap.process_id() != live_guardian:
			_fail("expired SGDX PID tracking lost a still-running native guardian")
		else:
			attempted_overlap.start(path)
			if attempted_overlap.is_active():
				attempted_overlap.stop("Rejected overlapping original native writer")
				_fail("Godot launched an overlapping writer despite live SGDX custody")
		if _error.is_empty():
			session.toggle_seat()
		if not (await _await_condition(Callable(self, "_seat_ready"))):
			_fail("real owner could not seat in original cockpit")
	if _error.is_empty():
		# Honor the existing 110ms original cockpit rate gate. An immediate
		# undock after SEAT ACK is correctly ignored by the production client.
		await create_timer(0.13).timeout
		session.submit_flight(PackedStringArray(["UND"]))
		if not (await _await_condition(Callable(self, "_undocked"))):
			_fail("original ship did not complete undock")
	if _error.is_empty():
		# Real owner must be able to return to the physically near station.
		# This is the ORIGINAL C++ docking action, not Godot teleportation.
		await create_timer(0.13).timeout
		session.submit_flight(PackedStringArray(["DCK"]))
		if not (await _await_condition(Callable(self, "_docked"))):
			_fail("original C++ return docking near station failed")
		elif _game._last_native_dock != 1 or not _game.ship_space.visible:
			_fail("real docking did not restore visible cabin state")
	if _error.is_empty():
		await create_timer(0.13).timeout
		session.submit_flight(PackedStringArray(["UND"]))
		if not (await _await_condition(Callable(self, "_undocked"))):
			_fail("second original launch after docking failed")
	if _error.is_empty():
		_game._external_requested = true
		_game._show_native_location(false)
		if not _game.space_preview.visible or not _game.exterior_camera.current or \
				_game.player.camera.current:
			_fail("real exterior V view did not activate the original ship camera")
	var collided := false
	var previous_x := -9223372036854775807
	if _error.is_empty():
		for i in range(70):
			# The original host owns every flight STEP; this intentionally
			# does NOT call main._on_native_ship_state with synthetic data.
			await create_timer(0.13).timeout
			session.submit_flight(PackedStringArray(["FWD"]))
			if not (await _await_condition(Callable(self, "_flight_done"))):
				_fail("native flight writer did not respond")
				break
			if not session._active or _ship_frames.is_empty() or _rock_frames.is_empty():
				_fail("native host disappeared during real flight")
				break
			var sh := _ship_frames[-1]
			var rock := _rock_frames[-1]
			var x := int(sh["x_mm"])
			if x < previous_x:
				_fail("original ship moved backward during positive thrust")
				break
			previous_x = x
			if int(rock.get("impact_rock", 0)) > 0:
				collided = true
				if int(rock["impact_tick"]) != int(sh["tick"]) or \
						int(rock["ship"]) != int(sh["ship"]) or \
						int(sh["vx"]) != 0 or int(sh["vy"]) != 0 or int(sh["vz"]) != 0:
					_fail("solid contact does not match the original committed ship tick")
					break
				if not bool(rock["found"]) or int(rock["ore_tonnes"]) <= 0:
					_fail("native original rock disappeared or mass was fabricated")
					break
				var relative_m := Vector3(float(rock["dx_mm"]),float(rock["dy_mm"]),
					float(rock["dz_mm"])).length() / 1000.0
				if relative_m < COLLISION_RADIUS_M - 0.002 or relative_m > COLLISION_RADIUS_M + 0.1:
					_fail("ship is not at the first safe original hull contact")
					break
				var relative_godot: Vector3 = MAIN.native_rock_visual_offset(rock)
				if _game.native_asteroid_mesh.position.distance_to(relative_godot) > 0.002:
					_fail("rendered asteroid does not track authoritative native delta")
					break
				if absf(_game.exterior_ship_visual.rotation.y -
						MAIN.native_heading_to_godot_yaw(int(sh["heading"]))) > 0.001:
					_fail("visual hull and camera do not follow original native yaw")
					break
				if not _game.exterior_camera.current or not _game.native_asteroid_mesh.visible:
					_fail("exterior camera or solid asteroid stopped being visible")
					break
				if not _game.asteroid_label.text.contains("ORIGINAL SOLID CONTACT"):
					_fail("original collision event not displayed by actual game HUD")
					break
				# The actual scene must derive target bearing and conservative
				# 5.2m-hull clearance only from this SAME verified ship/rock tick.
				if not is_instance_valid(_game.flight_instrument_label) or \
						not _game.flight_instrument_label.text.contains("TARGET #") or \
						not _game.flight_instrument_label.text.contains("VERIFIED IMPACT"):
					_fail("read-only pilot instrument did not join real original impact receipt")
					break
				var instrument: Dictionary = MAIN.FLIGHT_INSTRUMENTS.from_verified_frames(sh, rock)
				if instrument.is_empty() or \
						absf(float(instrument["clearance_m"])) > 0.1 or \
						absf(float(instrument["bearing_deg"])) > 5.0:
					_fail("real original-world target bearing or hull clearance is inconsistent")
					break
				break
	if _error.is_empty() and not collided:
		_fail("no real swept collision reached after 70 original forward commands")
	if is_instance_valid(_game) and is_instance_valid(_game.native_player_session):
		var sgdx_pid: int = _game.native_player_session.process_id()
		_game.native_player_session.stop("SGDX-03 integrated native acceptance closed", true)
		# A clean original QUIT must let SGDX write its final private receipt.
		if sgdx_pid > 0:
			for attempt in range(80):
				if not OS.is_process_running(sgdx_pid):
					break
				await create_timer(0.05).timeout
			if OS.is_process_running(sgdx_pid) and _error.is_empty():
				_fail("SGDX supervised original host failed to release after clean QUIT")
		if _error.is_empty():
			var directory := path + ".diagnostics"
			var found_real_receipt := false
			if DirAccess.dir_exists_absolute(directory):
				for file_name in DirAccess.get_files_at(directory):
					if not file_name.begins_with("process-") or not file_name.ends_with(".json"):
						continue
					var record = JSON.parse_string(FileAccess.get_file_as_string(
						directory.path_join(file_name)))
					if record is Dictionary and record.get("role", "") == "godot_ship" and \
							record.get("reason", "") == "clean_exit":
						found_real_receipt = true
						break
			if not found_real_receipt:
				_fail("Godot bypassed SGDX or clean original process evidence missing")
	await process_frame
	if not _error.is_empty():
		quit(1)
	else:
		print("FP3D-ENV-01E LIVE GODOT SCENE + ORIGINAL C++ HOST PASS: true undock, forward flight, safe contact, physical rock, camera yaw/translation and SQLite native receipts")
		quit(0)
