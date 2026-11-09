extends Node3D
## Self-contained monochrome FPS test arena: primitive meshes and true
## Godot physics collisions. NOT a replacement writer for native galaxy state.

const PLAYER_SCRIPT := preload("res://world/player_controller.gd")
const NATIVE_SESSION := preload("res://world/native_session.gd")
const NATIVE_PLAYER_SESSION := preload("res://world/native_player_session.gd")
const NATIVE_STATION_SESSION := preload("res://world/native_station_session.gd")
const REFINERY_TERMINAL_UI := preload("res://world/refinery_terminal_ui.gd")
const MONOCHROME := Color(0.76, 0.76, 0.76, 1.0)
# Visual capsule center for 1.75 m tall collider; original C++ X/Z unchanged.
const AVATAR_CENTER_HEIGHT_M := 0.90
var material: StandardMaterial3D
var player # assigned CharacterBody3D with runtime PlayerController.gd; signals are script-defined
var scope_label: Label
var status_label: Label
var fps_label: Label
var native_label: Label
var ship_label: Label
var station_space: Node3D
var ship_space: Node3D
var space_preview: Node3D
var native_asteroid_mesh: MeshInstance3D
var asteroid_label: Label
var _external_requested := false
var _last_native_dock := -1
var _geometry_parent: Node3D
var native_session
var native_player_session
var native_station_session
var refinery_terminal
var station_test_save_selected := false
var last_station_player_state := -1
# One SQLite world has only one native writer. Wait for the old child to
# actually terminate before launching the station/cockpit replacement.
var _handoff_target := ""
var _handoff_pid := -1
var _handoff_started_msec := 0
var native_markers: Node3D
var _fps_accumulator := 0.0

func _ready() -> void:
	material = StandardMaterial3D.new()
	material.albedo_color = MONOCHROME
	material.roughness = 1.0
	material.metallic = 0.0
	_setup_environment()
	station_space = Node3D.new()
	station_space.name = "StationInterior_Prototype"
	add_child(station_space)
	_geometry_parent = station_space
	_build_station()
	_build_objects()
	ship_space = Node3D.new()
	ship_space.name = "ShipInterior_Prototype"
	add_child(ship_space)
	_geometry_parent = ship_space
	_build_ship_interior()
	_geometry_parent = null
	_build_native_space_preview()
	_show_native_location(true) # station first; only native PERSON/POSE chooses later
	_build_player()
	_build_hud()
	# Native world observations are optional. The local character is NOT synchronized.
	native_markers = Node3D.new()
	native_markers.name = "NativeReadOnlyContacts_NOT_COLLISIONS"
	add_child(native_markers)
	native_session = NATIVE_SESSION.new()
	native_session.native_snapshot.connect(_on_native_snapshot)
	native_session.status_changed.connect(_on_native_status)
	native_player_session = NATIVE_PLAYER_SESSION.new()
	native_player_session.pose_received.connect(_on_native_player_pose)
	native_player_session.ship_state_received.connect(_on_native_ship_state)
	native_player_session.asteroid_contact_received.connect(_on_native_asteroid_contact)
	native_player_session.status_changed.connect(_on_native_player_status)
	native_player_session.connection_changed.connect(_on_native_player_connection)
	player.native_motion_intent.connect(_on_native_player_intent)
	player.native_seat_intent.connect(_on_native_player_seat_intent)
	player.native_ship_intents.connect(_on_native_ship_intents)
	native_station_session = NATIVE_STATION_SESSION.new()
	native_station_session.person_received.connect(_on_native_station_person)
	native_station_session.refinery_received.connect(_on_native_refinery_state)
	native_station_session.status_changed.connect(_on_native_player_status)
	native_station_session.connection_changed.connect(_on_native_station_connection)
	refinery_terminal.refine_requested.connect(_on_native_refine_requested)
	refinery_terminal.terminal_closed.connect(_hide_refinery_terminal)

func _setup_environment() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.10, 0.10, 0.10)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.63, 0.63, 0.63)
	env.ambient_light_energy = 0.7
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-52.0, -38.0, 0.0)
	sun.light_energy = 1.5
	sun.shadow_enabled = false
	add_child(sun)

func _solid_box(label: String, pos: Vector3, dimensions: Vector3) -> StaticBody3D:
	var body := StaticBody3D.new()
	body.name = label
	body.position = pos
	body.set_meta("inspect_label", label)
	body.collision_layer = 1
	var mesh := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = dimensions
	mesh.mesh = box
	mesh.material_override = material
	body.add_child(mesh)
	var collision := CollisionShape3D.new()
	var shape := BoxShape3D.new()
	shape.size = dimensions
	collision.shape = shape
	body.add_child(collision)
	if is_instance_valid(_geometry_parent):
		_geometry_parent.add_child(body)
	else:
		add_child(body)
	return body

func _solid_cylinder(label: String, pos: Vector3, height: float, radius: float) -> StaticBody3D:
	var body := StaticBody3D.new()
	body.name = label
	body.position = pos
	body.set_meta("inspect_label", label)
	body.collision_layer = 1
	var mesh := MeshInstance3D.new()
	var cyl := CylinderMesh.new()
	cyl.top_radius = radius
	cyl.bottom_radius = radius
	cyl.height = height
	cyl.radial_segments = 7
	mesh.mesh = cyl
	mesh.material_override = material
	body.add_child(mesh)
	var collider := CollisionShape3D.new()
	var shape := CylinderShape3D.new()
	shape.radius = radius
	shape.height = height
	collider.shape = shape
	body.add_child(collider)
	if is_instance_valid(_geometry_parent):
		_geometry_parent.add_child(body)
	else:
		add_child(body)
	return body

func _build_station() -> void:
	_solid_box("Station floor", Vector3(0, -0.25, 0), Vector3(64, 0.5, 64))
	# Four outer boundaries: no holes in the play test area.
	_solid_box("North perimeter", Vector3(0, 2.0, -32), Vector3(64, 4, 0.5))
	_solid_box("South perimeter", Vector3(0, 2.0, 32), Vector3(64, 4, 0.5))
	_solid_box("East perimeter", Vector3(32, 2.0, 0), Vector3(0.5, 4, 64))
	_solid_box("West perimeter", Vector3(-32, 2.0, 0), Vector3(0.5, 4, 64))
	# Wide-open doorway and a simple accessible station corridor.
	_solid_box("Hall west wall", Vector3(-5.0, 2.0, -7), Vector3(0.4, 4, 28))
	_solid_box("Hall east wall", Vector3(5.0, 2.0, -7), Vector3(0.4, 4, 28))
	_solid_box("Control room west", Vector3(-14, 2.0, -22), Vector3(18, 4, 0.4))
	_solid_box("Control room east", Vector3(14, 2.0, -22), Vector3(18, 4, 0.4))
	_solid_box("Hangar gangway", Vector3(0, 0.015, -4.5), Vector3(1.6, 0.03, 9))
	# Visual marker only. The original host authorizes access at (3m,0m).
	_solid_box("Station refinery terminal (R)", Vector3(3.0, 0.85, 0), Vector3(0.65, 1.7, 0.65))

func _build_objects() -> void:
	_solid_cylinder("NPC prototype — no arms", Vector3(0.0, 1.0, -5.0), 2, 0.38)
	_solid_cylinder("Life support module", Vector3(-2.5, 1.2, -11), 2.4, 0.8)
	_solid_box("Resource crate 01", Vector3(-2.3, 0.65, -2.5), Vector3(1.3, 1.3, 1.3))
	_solid_box("Resource crate 02", Vector3(3, 0.45, 0), Vector3(0.9, 0.9, 0.9))
	_solid_cylinder("Asteroid sample", Vector3(16, 1.3, 2), 2.6, 1.35)
	# A silhouette made from boxes to prove scale and collision, not an asset pipeline.
	# Visible hull aligned to the station hangar corridor. Boarding authority
	# remains the original native (0,0) hatch, NOT a local mesh hitbox.
	_solid_box("Docked owned ship hull (visual)", Vector3(0, 1.25, -11.0), Vector3(8, 2.5, 3.2))
	_solid_box("Ship wing L (visual)", Vector3(0, 0.65, -14.0), Vector3(4.2, 0.4, 4.0))
	_solid_box("Ship wing R (visual)", Vector3(0, 0.65, -8.0), Vector3(4.2, 0.4, 4.0))
	_solid_box("Docking control console", Vector3(3.9, 0.8, -0.5), Vector3(1.3, 1.6, 1.0))
	# Nonblocking floor marker aligned with native station hangar threshold.
	var hatch := MeshInstance3D.new()
	hatch.name = "Station_Hangar_Entry_At_Zero"
	var tile := BoxMesh.new()
	tile.size = Vector3(1.2, 0.015, 1.2)
	hatch.mesh = tile
	hatch.material_override = material
	hatch.position = Vector3(0.0, 0.02, 0.0)
	_geometry_parent.add_child(hatch)

func _build_ship_interior() -> void:
	# A separate visual cabin with the same 16 m prototype interior bounds as
	# native FP3D02C. Switching visibility is NEVER a custody operation.
	_solid_box("Ship interior floor", Vector3(0, -0.25, 0), Vector3(16, 0.5, 16))
	_solid_box("Ship interior port wall", Vector3(-8, 2, 0), Vector3(0.3, 4, 16))
	_solid_box("Ship interior starboard wall", Vector3(8, 2, 0), Vector3(0.3, 4, 16))
	_solid_box("Ship interior aft wall", Vector3(0, 2, 8), Vector3(16, 4, 0.3))
	_solid_box("Ship interior forward bulkhead", Vector3(0, 2, -8), Vector3(16, 4, 0.3))
	_solid_box("Native cockpit console (visual)", Vector3(0, 0.85, -2.1), Vector3(3.5, 1.4, 0.7))
	_solid_box("Cockpit windscreen upper frame", Vector3(0, 2.75, -5.5), Vector3(5.6, 0.2, 0.3))

func _build_native_space_preview() -> void:
	# External visual projection only, populated from the original C++ rock
	# contact. No static ore props, no Godot miner, no fake collision authority.
	space_preview = Node3D.new()
	space_preview.name = "NativeAsteroidExternalView"
	space_preview.visible = false
	add_child(space_preview)
	native_asteroid_mesh = MeshInstance3D.new()
	native_asteroid_mesh.name = "OriginalAsteroid_VisualOnly"
	var sphere := SphereMesh.new()
	sphere.radius = 1.0
	sphere.height = 2.0
	native_asteroid_mesh.mesh = sphere
	native_asteroid_mesh.material_override = material
	native_asteroid_mesh.visible = false
	space_preview.add_child(native_asteroid_mesh)

func _show_native_location(in_station: bool) -> void:
	# The only callers after genesis are verified native PERSON and POSE frames.
	# This never relocates a real PlayerPerson or writes a second world.
	if is_instance_valid(station_space):
		station_space.visible = in_station
		# Godot visibility alone does NOT disable hidden collision bodies.
		for body in station_space.get_children():
			if body is CollisionObject3D:
				body.collision_layer = 1 if in_station else 0
				body.collision_mask = 1 if in_station else 0
	var exterior := not in_station and _external_requested and _last_native_dock == 0
	if is_instance_valid(ship_space):
		ship_space.visible = not in_station and not exterior
		for body in ship_space.get_children():
			if body is CollisionObject3D:
				body.collision_layer = 0 if in_station or exterior else 1
				body.collision_mask = 0 if in_station or exterior else 1
	if is_instance_valid(space_preview):
		space_preview.visible = exterior

func _build_player() -> void:
	player = CharacterBody3D.new()
	player.name = "LocalGrayboxPerson_NOT_NATIVE_PLAYER"
	player.position = Vector3(0, AVATAR_CENTER_HEIGHT_M, 12.0)
	player.set_script(PLAYER_SCRIPT)
	add_child(player)
	player.inspected.connect(_on_inspected)

func _build_hud() -> void:
	var hud := CanvasLayer.new()
	add_child(hud)
	var panel := PanelContainer.new()
	panel.position = Vector2(15, 15)
	panel.custom_minimum_size = Vector2(680, 72)
	hud.add_child(panel)
	var contents := VBoxContainer.new()
	panel.add_child(contents)
	var title := Label.new()
	title.text = "SINGLE-GALAXY | STANDALONE GRAYBOX FPS 0.0.1"
	title.add_theme_font_size_override("font_size", 18)
	contents.add_child(title)
	scope_label = Label.new()
	scope_label.text = "LOCAL MOVEMENT TEST ONLY — NOT CONNECTED TO NATIVE GALAXY"
	contents.add_child(scope_label)
	status_label = Label.new()
	status_label.text = "H station, E board | R refinery terminal | P cockpit, E sit | WASD walk | A/D yaw, arrows thrust, B brake | G fabricate, F fit, M mine 1t, K power, U unfit"
	contents.add_child(status_label)
	native_label = Label.new()
	native_label.text = "NATIVE: H enters station player test. Walk toward marked hatch (0,0), E boards. P switches to ship cockpit."
	contents.add_child(native_label)
	asteroid_label = Label.new()
	asteroid_label.text = "ASTEROID: awaiting verified original ship contact"
	contents.add_child(asteroid_label)
	ship_label = Label.new()
	ship_label.text = "SHIP: native physical state not yet verified"
	contents.add_child(ship_label)
	fps_label = Label.new()
	fps_label.position = Vector2(16, 105)
	fps_label.add_theme_font_size_override("font_size", 15)
	hud.add_child(fps_label)
	var reticle := Label.new()
	reticle.text = "+"
	reticle.add_theme_font_size_override("font_size", 25)
	reticle.set_anchors_and_offsets_preset(Control.PRESET_CENTER)
	hud.add_child(reticle)
	refinery_terminal = REFINERY_TERMINAL_UI.new()
	hud.add_child(refinery_terminal)

func _on_inspected(display_name: String) -> void:
	status_label.text = "Inspected: " + display_name + " (prototype only; no native action)"

func _schedule_native_handoff(target: String, outgoing_pid: int) -> void:
	# Process shutdown is not proof that SQLite's WAL writer was released.
	# Never start a replacement child until the outgoing PID is truly dead.
	_handoff_target = target
	_handoff_pid = outgoing_pid
	_handoff_started_msec = Time.get_ticks_msec()
	if is_instance_valid(player):
		player.use_native_authority(true) # no unjournaled walking during handoff
	_advance_native_handoff()

func _advance_native_handoff() -> void:
	if _handoff_target.is_empty():
		return
	if _handoff_pid > 0 and OS.is_process_running(_handoff_pid):
		if Time.get_ticks_msec() - _handoff_started_msec > 3000:
			# Fail closed, rather than launching two original-world writers.
			_handoff_target = ""
			_on_native_player_status("Native writer handoff aborted: previous process still alive; world unchanged")
			if is_instance_valid(player):
				player.use_native_authority(false)
		return
	var destination := _handoff_target
	_handoff_target = ""
	_handoff_pid = -1
	if destination == "station":
		station_test_save_selected = true
		last_station_player_state = -1 # only fresh native PERSON can unlock P
		native_station_session.start()
	elif destination == "ship":
		var mining_save := OS.get_environment("SG_04B_TEST_SAVE")
		var save_override := mining_save if not mining_save.is_empty() else OS.get_user_data_dir().path_join("single_galaxy_station_test.sqlite") if station_test_save_selected else ""
		native_player_session.start(save_override)
	# If launching the replacement failed, do not keep an inert authority lock.
	if is_instance_valid(player) and not native_station_session.is_active() and not native_player_session.is_active():
		player.use_native_authority(false)

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo and event.keycode in [KEY_M, KEY_F, KEY_G, KEY_K, KEY_U]:
		if native_player_session == null or not native_player_session.is_active():
			_on_native_player_status("Mining requires the P native cockpit and an existing owned ship")
		else:
			match event.keycode:
				KEY_M:
					native_player_session.submit_mining()
				KEY_F:
					native_player_session.submit_fitting()
				KEY_G:
					native_player_session.submit_fabrication()
				KEY_K:
					native_player_session.toggle_mining_power()
				KEY_U:
					native_player_session.submit_fitting(true)
		get_viewport().set_input_as_handled()
		return
	if event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_V:
		if native_player_session != null and native_player_session.is_active() and _last_native_dock == 0 and last_station_player_state == 0:
			_external_requested = not _external_requested
			_show_native_location(false)
		else:
			_on_native_player_status("External V view requires your real undocked ship")
		get_viewport().set_input_as_handled()
		return
	if event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_R:
		if native_station_session == null or not native_station_session.is_active() or last_station_player_state != 4:
			_on_native_player_status("Connect as a real station resident with H before using refinery")
		else:
			if is_instance_valid(refinery_terminal) and refinery_terminal.visible:
				_hide_refinery_terminal()
			else:
				refinery_terminal.show_waiting()
				Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
				native_station_session.request_refinery_view()
		get_viewport().set_input_as_handled()
		return
	if event is InputEventKey and event.pressed and not event.echo and (event.keycode == KEY_H or event.keycode == KEY_P):
		if not _handoff_target.is_empty():
			_on_native_player_status("Native writer change in progress; original save remains locked to one writer")
			get_viewport().set_input_as_handled()
			return
	if event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_H:
		if native_station_session.is_active():
			native_station_session.stop("Station test session stopped")
		else:
			var exiting_pid: int = native_player_session.process_id()
			if native_player_session.is_active():
				native_player_session.stop("Ship session closed before station connection")
			if native_session.is_active():
				native_session.stop("Observer closed before station connection")
			_schedule_native_handoff("station", exiting_pid)
		get_viewport().set_input_as_handled()
		return
	if event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_P:
		if native_player_session.is_active():
			native_player_session.stop("NATIVE FPS stopped by player; pose saved")
		else:
			if station_test_save_selected and last_station_player_state != 0:
				_on_native_player_status("Board your existing ship first; cockpit is unavailable while at station")
				get_viewport().set_input_as_handled()
				return
			var exiting_pid: int = native_station_session.process_id()
			if native_station_session.is_active():
				native_station_session.stop("Station writer closed before ship cockpit session")
			if native_session.is_active():
				native_session.stop("Read-only observer stopped for native FPS writer")
			_schedule_native_handoff("ship", exiting_pid)
		get_viewport().set_input_as_handled()
		return
	if event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_O:
		if native_player_session.is_active():
			_on_native_status("O read-only disabled while P authoritative writer is active")
			return
		if native_session.is_active():
			native_session.stop("READ ONLY — stopped by player; last verified frame retained; O reconnects")
		else:
			native_session.start()
		get_viewport().set_input_as_handled()

func _on_native_station_connection(connected: bool) -> void:
	if not connected:
		_hide_refinery_terminal()
	if is_instance_valid(player):
		player.use_native_authority(connected)
	if is_instance_valid(scope_label):
		scope_label.text = "NATIVE STATION PLAYER (ORIGINAL SQLITE)" if connected else "STATION NATIVE DISCONNECTED"

func _on_native_refinery_state(snapshot: Dictionary) -> void:
	# Original C++ station host performed all authorization and inventory changes.
	if native_station_session == null or not native_station_session.is_active() or last_station_player_state != 4:
		return
	_record_native_projection("REFINERY", snapshot)
	if is_instance_valid(refinery_terminal):
		refinery_terminal.present(snapshot)

func _on_native_refine_requested(lot_id: int) -> void:
	# No client-side yields or refining; send only the selected original lot ID.
	if native_station_session != null and native_station_session.is_active() and last_station_player_state == 4:
		native_station_session.submit_refining(lot_id)

func _hide_refinery_terminal() -> void:
	if is_instance_valid(refinery_terminal):
		refinery_terminal.visible = false
	if is_instance_valid(player) and get_window().has_focus():
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED

func _record_native_projection(kind: String, evidence: Dictionary) -> void:
	# CLIENT-OBSERVED evidence only; original SQLite journal remains authoritative.
	# Bounded local file for Windows physical playtest diagnosis, never a second save.
	var path := "user://sg_station_visual_acceptance.jsonl"
	var f := FileAccess.open(path, FileAccess.READ_WRITE)
	if f == null:
		f = FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return
	if f.get_length() > 1048576:
		f.close()
		f = FileAccess.open(path, FileAccess.WRITE)
		if f == null:
			return
	f.seek_end()
	f.store_line(JSON.stringify({"source":"godot-observed-native-frame",
		"event":kind,"wall_unix":Time.get_unix_time_from_system(),"frame":evidence}))
	f.close()

func _on_native_station_person(person: Dictionary) -> void:
	_record_native_projection("PERSON", person)
	last_station_player_state = int(person["state"])
	if last_station_player_state != 4:
		_hide_refinery_terminal()
	_show_native_location(last_station_player_state == 4)
	if is_instance_valid(player):
		player.use_native_seat(false)
		player.global_position = Vector3(float(person["x_mm"]) / 1000.0, AVATAR_CENTER_HEIGHT_M,
			float(person["z_mm"]) / 1000.0)
	if is_instance_valid(ship_label):
		ship_label.text = "STATION #%d | %s | available owned ship #%d | position (%.2f, %.2f)m" % [
			int(person["station"]), "ON FOOT" if int(person["state"]) == 4 else "ABOARD SHIP",
			int(person["eligible_ship"]), float(person["x_mm"])/1000.0,
			float(person["z_mm"])/1000.0]

func _on_native_player_connection(connected: bool) -> void:
	# Process start alone is NOT proof of ship custody; wait for POSE.
	if not connected:
		_external_requested = false
		if is_instance_valid(native_asteroid_mesh):
			native_asteroid_mesh.visible = false
		if not station_test_save_selected:
			_show_native_location(true)
	if is_instance_valid(player):
		player.use_native_authority(connected)
	if not connected and is_instance_valid(ship_label):
		ship_label.text = "SHIP: native host disconnected; previous readout not live"
	if is_instance_valid(scope_label):
		scope_label.text = "NATIVE IN-SHIP POSE + ORIGINAL C++ FLIGHT (TEST GEOMETRY)" if connected else "LOCAL MOVEMENT TEST ONLY — NOT CONNECTED TO NATIVE GALAXY"

func _on_native_ship_intents(actions: PackedStringArray) -> void:
	if native_player_session != null:
		native_player_session.submit_flight(actions)

func _on_native_ship_state(ship: Dictionary) -> void:
	_record_native_projection("SHIP", ship)
	if not is_instance_valid(ship_label):
		return
	var states := ["IN SPACE", "DOCKED", "UNDOCKING", "DOCKING"]
	var dock: int = int(ship["dock"])
	_last_native_dock = dock
	if dock != 0:
		_external_requested = false
	_show_native_location(last_station_player_state == 4)
	ship_label.text = "ORIGINAL SHIP #%d | %s | yaw %d° | position (%.1f, %.1f, %.1f)m | velocity (%d, %d, %d)mm/tick" % [
		int(ship["ship"]), states[dock], int(ship["heading"]),
		float(ship["x_mm"]) / 1000.0, float(ship["y_mm"]) / 1000.0,
		float(ship["z_mm"]) / 1000.0,
		int(ship["vx"]), int(ship["vy"]), int(ship["vz"])]

func _on_native_asteroid_contact(contact: Dictionary) -> void:
	# Real original ship snapshot, never a client-spawned resource.
	if native_player_session == null or not native_player_session.is_active():
		return
	_record_native_projection("ROCK", contact)
	if not is_instance_valid(native_asteroid_mesh):
		return
	var found := bool(contact["found"])
	native_asteroid_mesh.visible = found
	if found:
		# Original world X/Y/Z -> Godot horizontal X/Z and vertical Y.
		# Relative render projection stays within 30 metres of native ship.
		native_asteroid_mesh.position = Vector3(
			float(contact["dx_mm"]) / 1000.0, float(contact["dz_mm"]) / 1000.0,
			-float(contact["dy_mm"]) / 1000.0)
		var visual_radius := maxf(0.3, float(contact["radius_mm"]) / 1000.0)
		native_asteroid_mesh.scale = Vector3.ONE * visual_radius
		var range_m := Vector3(float(contact["dx_mm"]),float(contact["dy_mm"]),float(contact["dz_mm"])).length() / 1000.0
		asteroid_label.text = "ROCK #%d | %.1fm | %d t finite ore | ship cargo %d/%d t | mining tool: %s | V exterior" % [
			int(contact["rock"]),range_m,int(contact["ore_tonnes"]),int(contact["cargo_tonnes"]),
			int(contact["cargo_capacity"]),"ONLINE" if int(contact["mining_tool_online"]) == 1 else "OFFLINE/NOT FITTED"]
	else:
		asteroid_label.text = "No original asteroid within 30m | ship cargo %d/%d t | mining tool: %s" % [
			int(contact["cargo_tonnes"]),int(contact["cargo_capacity"]),
			"ONLINE" if int(contact["mining_tool_online"]) == 1 else "OFFLINE/NOT FITTED"]

func _on_native_player_seat_intent() -> void:
	if native_station_session != null and native_station_session.is_active():
		native_station_session.interact_hangar()
	elif native_player_session != null:
		native_player_session.toggle_seat()

func _on_native_player_intent(heading: Vector3) -> void:
	if native_station_session != null and native_station_session.is_active():
		native_station_session.submit_walk(heading)
	elif native_player_session != null:
		native_player_session.submit_motion(heading)

func _on_native_player_pose(pose: Dictionary) -> void:
	_record_native_projection("POSE", pose)
	_show_native_location(false) # verified original owned ship pose
	# The Godot avatar is positioned from ACKNOWLEDGED native SQL state ONLY.
	# Vertical eye/physics meshes remain placeholder until full 3D native geometry.
	if is_instance_valid(player):
		player.use_native_seat(bool(pose["seated"]))
		player.global_position = Vector3(float(pose["x_mm"]) / 1000.0,
			AVATAR_CENTER_HEIGHT_M, float(pose["z_mm"]) / 1000.0)

func _on_native_player_status(message: String) -> void:
	if is_instance_valid(native_label):
		native_label.text = message
	if is_instance_valid(refinery_terminal) and refinery_terminal.visible and ("refused" in message or "timed out" in message):
		refinery_terminal.report_error(message)

func _on_native_status(message: String) -> void:
	if is_instance_valid(native_label):
		native_label.text = "NATIVE: " + message
		# Keep the reason visible until another explicit user action changes it.

func _on_native_snapshot(frame: Dictionary) -> void:
	# Render verified native contacts as NON-COLLIDING visual indicators only.
	# Native SGVIEW X/Y horizontal millimeters are mapped to viewer X/Z. Y=3 is
	# explicitly illustrative, NOT an authoritative 3D player or ship altitude.
	if is_instance_valid(native_label):
		native_label.text = "NATIVE READ ONLY (O stop): tick %d | system %d | ship #%d | %d contacts" % [
			frame["tick"], frame["system"], frame["ship_id"], frame["contacts"].size()]
	for child in native_markers.get_children():
		child.queue_free()
	for c in frame["contacts"]:
		var dx := float(c["x_mm"] - frame["x_mm"]) / 1000.0
		var dz := -float(c["y_mm"] - frame["y_mm"]) / 1000.0
		if absf(dx) > 120.0 or absf(dz) > 120.0:
			continue
		var marker := MeshInstance3D.new()
		marker.name = "ReadOnlyContact_%d" % c["id"]
		var primitive := SphereMesh.new()
		primitive.radius = 0.4 if c["kind"] == 0 else 0.7
		primitive.height = primitive.radius * 2.0
		primitive.radial_segments = 6
		primitive.rings = 2
		marker.mesh = primitive
		marker.material_override = material
		marker.position = Vector3(dx, 3.0, dz)
		native_markers.add_child(marker)

func _exit_tree() -> void:
	if native_session != null:
		native_session.stop("Native client closed")
	if native_player_session != null:
		native_player_session.stop("Native FPS client closed")
	if native_station_session != null:
		native_station_session.stop("Native station client closed")

func _process(delta: float) -> void:
	_advance_native_handoff()
	if native_session != null:
		native_session.poll()
	if native_player_session != null:
		native_player_session.poll()
	if native_station_session != null:
		native_station_session.poll()

	_fps_accumulator += delta
	if _fps_accumulator >= 0.5 and is_instance_valid(fps_label):
		fps_label.text = "FPS: %d | Godot physics 60 Hz | primitive monochrome scene" % Engine.get_frames_per_second()
		_fps_accumulator = 0.0
