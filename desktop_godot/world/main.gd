extends Node3D
## Self-contained two-tone gray station / white ship FPS: primitive meshes and true
## Godot physics collisions. NOT a replacement writer for native galaxy state.

const PLAYER_SCRIPT := preload("res://world/player_controller.gd")
const NATIVE_SESSION := preload("res://world/native_session.gd")
const NATIVE_PLAYER_SESSION := preload("res://world/native_player_session.gd")
const NATIVE_STATION_SESSION := preload("res://world/native_station_session.gd")
const REFINERY_TERMINAL_UI := preload("res://world/refinery_terminal_ui.gd")
const FLIGHT_INSTRUMENTS := preload("res://world/native_flight_instruments.gd")
const INTERACTION_PROMPTS := preload("res://world/interaction_prompts.gd")
const COLONY_DOCK_LAYOUT := preload("res://world/colony_dock_layout.gd")
const STATION_GRAY := Color(0.47, 0.51, 0.55, 1.0)
const SHIP_WHITE := Color(0.96, 0.97, 0.99, 1.0)
# Visual capsule center for 1.75 m tall collider; original C++ X/Z unchanged.
const AVATAR_CENTER_HEIGHT_M := 0.90
var material: StandardMaterial3D
var ship_material: StandardMaterial3D
var flight_panel: PanelContainer
var player # assigned CharacterBody3D with runtime PlayerController.gd; signals are script-defined
var scope_label: Label
var status_label: Label
var interaction_label: Label
var retry_station_button: Button
var fps_label: Label
var native_label: Label
var ship_label: Label
var station_space: Node3D
var ship_space: Node3D
var space_preview: Node3D
var native_asteroid_mesh: MeshInstance3D
var exterior_ship_visual: Node3D
var exterior_camera: Camera3D
var asteroid_label: Label
var flight_instrument_label: Label
var _verified_flight_ship: Dictionary = {}
var _verified_flight_rock: Dictionary = {}
# The actual hull yaw is always set immediately from the committed C++ SHIP
# receipt; ONLY the rendered exterior camera eases back toward its mount.
var _exterior_camera_rest := Transform3D.IDENTITY
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
# A real space -> dock transition arms on-foot return. Never infer it from
# the client camera or a mere docked snapshot at initial cockpit start.
var _colony_return_armed := false
var _colony_return_pending := false
# Boarding automatically requests exactly one original native SEAT.
# A refused seat is not retried in a loop: keep the player locked in the
# third-person pilot view until the saved state is independently corrected.
var _pilot_auto_seat_attempted := false
var native_markers: Node3D
var _fps_accumulator := 0.0
# The Godot sidecar is diagnostics, NEVER an alternative SQLite world.
# Buffer native-observed receipts; synchronous open/write/close for every
# SHIP + ROCK ack caused unnecessary per-command desktop I/O jitter.
var _native_visual_evidence_buffer := PackedStringArray()
var _last_visual_evidence_flush_ms := 0
const NATIVE_EVIDENCE_FLUSH_INTERVAL_MS := 500
const NATIVE_EVIDENCE_BUFFER_LIMIT := 64
# Former developer-only H toggle is no longer needed during normal play.
# Standalone script tests keep native fixtures under explicit test control.
var _player_session_autoconnect := false
var _station_connect_attempted := false
var _station_connection_failure := ""

func _ready() -> void:
	material = StandardMaterial3D.new()
	material.albedo_color = STATION_GRAY
	material.roughness = 1.0
	material.metallic = 0.0
	ship_material = StandardMaterial3D.new()
	ship_material.albedo_color = SHIP_WHITE
	ship_material.roughness = 0.78
	ship_material.metallic = 0.06
	_setup_environment()
	station_space = Node3D.new()
	station_space.name = "StationInterior_Prototype"
	add_child(station_space)
	_geometry_parent = station_space
	_build_station()
	_build_objects()
	# Procedural, visual-only colony -> dock -> berth transition. Native
	# station-person coordinates, SQLite ownership and boarding are unchanged.
	COLONY_DOCK_LAYOUT.build(station_space)
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
	native_station_session.status_changed.connect(_on_native_station_status)
	native_station_session.connection_changed.connect(_on_native_station_connection)
	refinery_terminal.refine_requested.connect(_on_native_refine_requested)
	refinery_terminal.unload_requested.connect(_on_native_unload_requested)
	refinery_terminal.terminal_closed.connect(_hide_refinery_terminal)
	_player_session_autoconnect = not Engine.is_editor_hint() and DisplayServer.get_name() != "headless" and not OS.get_cmdline_args().has("--script")
	if _player_session_autoconnect:
		call_deferred("_open_saved_station_automatically")

func _open_saved_station_automatically() -> void:
	if _station_connect_attempted:
		return
	_station_connect_attempted = true
	_station_connection_failure = ""
	_on_native_player_status("Connecting to your original saved station...")
	# One SQLite writer, whether we are launching or changing roles.
	# A missing, damaged or locked save is NEVER automatically replaced.
	_schedule_native_handoff("station", native_player_session.process_id())

func _retry_original_station() -> void:
	if native_station_session.is_active():
		_on_native_player_status("Station already connected. Walk to the cyan boarding hatch and press E.")
		return
	if native_player_session.is_active():
		_on_native_player_status("Your original ship is already connected. Press E at the cockpit seat.")
		return
	_station_connection_failure = ""
	_station_connect_attempted = true
	if is_instance_valid(retry_station_button):
		retry_station_button.visible = false
	_on_native_player_status("Retrying your saved station without resetting the world...")
	_schedule_native_handoff("station", native_station_session.process_id())

func _enter_cockpit_after_verified_board() -> void:
	if not _player_session_autoconnect or not native_station_session.is_active() or last_station_player_state != 0 or not _handoff_target.is_empty():
		return
	_on_native_player_status("Boarding confirmed by original C++ — opening your ship cockpit...")
	var exiting_pid: int = native_station_session.process_id()
	native_station_session.stop("Native boarding committed; transferring verified SQLite writer", true)
	_schedule_native_handoff("ship", exiting_pid)

func _complete_docked_disembark_after_native_seat_release() -> void:
	# The user never stands/explores inside the ship. A single E at the
	# verified returned dock initiated the original C++ seat-release receipt;
	# only that receipt permits closing the cockpit writer and trying EXIT.
	if not _colony_return_pending or _last_native_dock != 1 or \
			native_player_session == null or not native_player_session.is_active() or \
			native_player_session._seated or native_player_session._pending != 0 or \
			!_handoff_target.is_empty():
		return
	var outgoing_pid: int = native_player_session.process_id()
	native_player_session.stop("Verified docked pilot EXIT; releasing original SQLite writer", true)
	_schedule_native_handoff("station", outgoing_pid)

func _ensure_native_pilot_role() -> void:
	# Live standalone cockpit ONLY: never issue a synthetic seat receipt.
	# Tests with explicitly managed native drivers keep their own control.
	if not _player_session_autoconnect or _pilot_auto_seat_attempted or \
			_colony_return_pending or last_station_player_state != 0 or \
			native_player_session == null or not native_player_session.is_active() or \
			native_player_session._session <= 0 or \
			not native_player_session._initial_ship_seen or \
			native_player_session._pending != 0 or native_player_session._seated:
		return
	_pilot_auto_seat_attempted = true
	native_player_session.toggle_seat()

func _exit_docked_ship_after_verified_station_person() -> void:
	if not _colony_return_pending or not native_station_session.is_active() or last_station_player_state != 0:
		return
	# C++ independently checks real station/ship ownership and dock state.
	# A failed or denied EXIT never creates a local walking player.
	native_station_session.interact_hangar()

func _setup_environment() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.15, 0.19, 0.25)
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
	# The owned docked hull and actual cockpit have a consistent white visual
	# identity. Station architecture remains gray. Materials are presentation
	# only and never imply a second physical vessel.
	var white_hull: bool = _geometry_parent == ship_space or label.begins_with("Docked owned ship") or label.begins_with("Ship wing")
	mesh.material_override = ship_material if white_hull else material
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
	# Open-air surface colony: perimeter barriers, rather than a giant
	# enclosed box. The native +/-5 m corridor remains the REAL walk fence.
	_solid_box("North perimeter", Vector3(0, 0.75, -32), Vector3(64, 1.5, 0.5))
	_solid_box("South perimeter", Vector3(0, 0.75, 32), Vector3(64, 1.5, 0.5))
	_solid_box("East perimeter", Vector3(32, 0.75, 0), Vector3(0.5, 1.5, 64))
	_solid_box("West perimeter", Vector3(-32, 0.75, 0), Vector3(0.5, 1.5, 64))
	# These gray low walls correspond to the existing original C++ native
	# hallway blockers at x=-5m and x=+5m. The colony blocks beyond are
	# visual skyline, not pass-through colliding fake structures.
	_solid_box("Hall west wall", Vector3(-5.0, 0.65, -7), Vector3(0.4, 1.3, 28))
	_solid_box("Hall east wall", Vector3(5.0, 0.65, -7), Vector3(0.4, 1.3, 28))
	_solid_box("Control room west", Vector3(-14, 0.8, -22), Vector3(18, 1.6, 0.4))
	_solid_box("Control room east", Vector3(14, 0.8, -22), Vector3(18, 1.6, 0.4))
	_solid_box("Hangar gangway", Vector3(0, 0.015, -4.5), Vector3(1.6, 0.03, 9))
	# Visual marker only. The original host authorizes access at (3m,0m).
	_solid_box("Station refinery terminal (R)", Vector3(3.0, 0.85, 0), Vector3(0.65, 1.7, 0.65))
	# Actual C++ station-person walk envelope is +/-8m in both axes.
	# Visual perimeter strips are noncolliding: only the native original
	# player transaction owns movement admission and station obstacles.
	var native_limit := Node3D.new()
	native_limit.name = "NativeStationWalkEnvelope_16m_VisualOnly"
	_geometry_parent.add_child(native_limit)
	for i in range(4):
		var strip := MeshInstance3D.new()
		strip.name = "native_walk_boundary_" + str(i)
		var bar := BoxMesh.new()
		bar.size = Vector3(16.0, 0.02, 0.04) if i < 2 else Vector3(0.04, 0.02, 16.0)
		strip.mesh = bar
		strip.material_override = material
		strip.position = Vector3(0.0, 0.025, -8.0 if i == 0 else 8.0) if i < 2 else Vector3(-8.0 if i == 2 else 8.0, 0.025, 0.0)
		native_limit.add_child(strip)

func _build_objects() -> void:
	_solid_cylinder("NPC prototype — no arms", Vector3(0.0, 1.0, -5.0), 2, 0.38)
	_solid_cylinder("Life support module", Vector3(-2.5, 1.2, -11), 2.4, 0.8)
	_solid_box("Resource crate 01", Vector3(-2.3, 0.65, -2.5), Vector3(1.3, 1.3, 1.3))
	_solid_box("Resource crate 02", Vector3(3, 0.45, 0), Vector3(0.9, 0.9, 0.9))
	_solid_cylinder("Asteroid sample", Vector3(16, 1.3, 2), 2.6, 1.35)
	# Actual original owned ShipID is now legible as a white landed craft on
	# the DOCK deck, within sight of colony arrival and its native boarding
	# checkpoint. The dark canopy/landing legs live in visual-only layout data.
	# BOARD is still granted ONLY by original C++ at station (0,0).
	_solid_box("Docked owned ship hull (visual)", Vector3(0, 1.34, -4.90), Vector3(3.0, 2.35, 4.0))
	_solid_box("Ship wing L (visual)", Vector3(-2.05, 0.67, -5.35), Vector3(2.4, 0.35, 2.7))
	_solid_box("Ship wing R (visual)", Vector3(2.05, 0.67, -5.35), Vector3(2.4, 0.35, 2.7))
	# The original refinery terminal remains x=3m,z=0. Dock service
	# consoles are decorative; don't invent a second authoritative control.
	_solid_box("Docking control console", Vector3(3.9, 0.8, -0.5), Vector3(1.3, 1.6, 1.0))
	# Nonblocking floor marker aligned with native station hangar threshold.
	var hatch := MeshInstance3D.new()
	hatch.name = "Station_Hangar_Entry_At_Zero"
	var tile := BoxMesh.new()
	tile.size = Vector3(1.2, 0.015, 1.2)
	hatch.mesh = tile
	# Make the real native (0,0) boarding threshold visible against the
	# monochrome ship hull. This mesh is NON-COLLIDING and cannot authorize E.
	var hatch_color := StandardMaterial3D.new()
	hatch_color.albedo_color = Color(0.08, 0.89, 0.96, 1.0)
	hatch_color.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	hatch.material_override = hatch_color
	# The gangway is a solid surface with top Y=0.03 m. The former marker
	# ended at Y=0.0275 m: hidden UNDER that floor! Its top is now 0.085 m.
	# The marker has no collision, and the server's 600mm hatch stays (0,0).
	hatch.position = Vector3(0.0, 0.0775, 0.0)
	_geometry_parent.add_child(hatch)
	var beacon := Label3D.new()
	beacon.name = "BoardingHatch_WorldSign_VisualOnly"
	beacon.text = "BOARDING HATCH\nE WHEN PROMPTED"
	beacon.position = Vector3(0.0, 2.15, 0.0)
	beacon.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	beacon.pixel_size = 0.006
	beacon.font_size = 42
	beacon.modulate = Color(0.12, 1.0, 1.0, 1.0)
	beacon.no_depth_test = true
	_geometry_parent.add_child(beacon)

func _build_ship_interior() -> void:
	# A separate visual cabin with the same 16 m prototype interior bounds as
	# native FP3D02C. Switching visibility is NEVER a custody operation.
	_solid_box("Ship interior floor", Vector3(0, -0.25, 0), Vector3(16, 0.5, 16))
	_solid_box("Ship interior port wall", Vector3(-8, 2, 0), Vector3(0.3, 4, 16))
	_solid_box("Ship interior starboard wall", Vector3(8, 2, 0), Vector3(0.3, 4, 16))
	_solid_box("Ship interior aft wall", Vector3(0, 2, 8), Vector3(16, 4, 0.3))
	_solid_box("Ship interior forward bulkhead", Vector3(0, 2, -8), Vector3(16, 4, 0.3))
	_solid_box("Native cockpit console (visual)", Vector3(0, 0.85, -2.1), Vector3(3.5, 1.4, 0.7))
	# The native original movement collision map has a crate at X=1.2..2.0m,
	# Z=-0.5..0.5m. Draw/collide at those exact dimensions, not a phantom wall.
	_solid_box("Native interior cargo obstruction", Vector3(1.6, 0.8, 0), Vector3(0.8, 1.6, 1.0))
	_solid_box("Cockpit windscreen upper frame", Vector3(0, 2.75, -5.5), Vector3(5.6, 0.2, 0.3))

static func native_rock_visual_offset(contact: Dictionary) -> Vector3:
	# Original X/Y/Z in millimeters -> Godot local X/-Z and up Y in metres.
	# This is a camera-relative projection, not physics or world navigation.
	return Vector3(float(contact["dx_mm"]) / 1000.0,
		float(contact["dz_mm"]) / 1000.0,
		-float(contact["dy_mm"]) / 1000.0)

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
	# A sparse NON-COLLIDING inertial orientation reference makes real
	# native yaw visible when no C++ asteroid exists inside the 30m sensor.
	# These dots are NOT asteroid resources, pilot targets or world physics.
	var orientation_guides := MultiMeshInstance3D.new()
	orientation_guides.name = "VisualOnlyBearingDots_NOT_NativeContacts"
	var guide_sphere := SphereMesh.new()
	guide_sphere.radius = 0.66
	guide_sphere.height = 1.32
	guide_sphere.radial_segments = 8
	guide_sphere.rings = 4
	var guide_material := StandardMaterial3D.new()
	guide_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	guide_material.albedo_color = Color(0.40, 0.69, 0.84, 1.0)
	orientation_guides.material_override = guide_material
	var dot_meshes := MultiMesh.new()
	dot_meshes.transform_format = MultiMesh.TRANSFORM_3D
	dot_meshes.mesh = guide_sphere
	dot_meshes.instance_count = 20
	for index in range(20):
		var angle := TAU * float(index) / 20.0
		var inertial_location := Vector3(cos(angle) * 67.0,
			sin(angle * 3.0) * 11.0, sin(angle) * 67.0)
		dot_meshes.set_instance_transform(index,
			Transform3D(Basis.IDENTITY, inertial_location))
	orientation_guides.multimesh = dot_meshes
	space_preview.add_child(orientation_guides)
	# Greybox ship hull is 8m long, 2.5m high, with visible wings spanning
	# ~6m. The ORIGINAL C++ physics uses a conservative 5.2m bounding sphere.
	# Neither these meshes nor the camera writes any collision or ship pose.
	exterior_ship_visual = Node3D.new()
	exterior_ship_visual.name = "OriginalShip_VisualHullOnly"
	space_preview.add_child(exterior_ship_visual)
	var body := MeshInstance3D.new()
	body.name = "8m native hull proxy"
	var body_mesh := BoxMesh.new()
	body_mesh.size = Vector3(8.0, 2.5, 3.0)
	body.mesh = body_mesh
	body.material_override = ship_material
	exterior_ship_visual.add_child(body)
	for side in [-1,1]:
		var wing := MeshInstance3D.new()
		wing.name = "Native hull wing" + str(side)
		var wing_mesh := BoxMesh.new()
		wing_mesh.size = Vector3(5.5, 0.3, 1.4)
		wing.mesh = wing_mesh
		wing.material_override = ship_material
		wing.position = Vector3(-0.25, -0.45, float(side) * 2.35)
		exterior_ship_visual.add_child(wing)
	exterior_camera = Camera3D.new()
	exterior_camera.name = "OriginalShip_ThirdPersonCamera"
	exterior_camera.position = Vector3(-16.0, 7.0, 0.0)
	exterior_camera.fov = 72.0
	exterior_ship_visual.add_child(exterior_camera)
	exterior_camera.look_at(space_preview.global_position + Vector3(0.0, 0.6, 0.0), Vector3.UP)
	_exterior_camera_rest = exterior_camera.transform
	exterior_camera.current = false

static func native_heading_to_godot_yaw(heading: int) -> float:
	# Original yaw 0 = +X; 90 = +native Y = -Godot Z.
	return deg_to_rad(float(heading % 360))

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
	# PILOT-ONLY: third-person external camera in every native aboard
	# state, whether docked, undocking or flying. No cabin exploration mode
	# and no V toggle; the native C++ hull heading remains authoritative.
	var exterior := not in_station
	if is_instance_valid(ship_space):
		ship_space.visible = not in_station and not exterior
		for body in ship_space.get_children():
			if body is CollisionObject3D:
				body.collision_layer = 0 if in_station or exterior else 1
				body.collision_mask = 0 if in_station or exterior else 1
	if is_instance_valid(flight_panel):
		flight_panel.visible = not in_station and not _verified_flight_ship.is_empty()
	if is_instance_valid(space_preview):
		space_preview.visible = exterior
		if exterior and is_instance_valid(player):
			# Anchor the read-only ship/rock pair to the viewer. Local rock XYZ
			# stays in inertial space; only the visual hull/camera rotate with
			# the trusted original C++ heading.
			space_preview.global_position = player.global_position
	if is_instance_valid(exterior_camera) and is_instance_valid(player) and is_instance_valid(player.camera):
		exterior_camera.current = exterior
		player.camera.current = not exterior

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
	# Keep status in one compact top-left card, while ship-only telemetry is
	# in a distinct top-right card. No diagnostic row has an absolute Y value
	# overlapping the dynamic text above it (observed on physical Radeon PC).
	var panel := PanelContainer.new()
	panel.name = "SystemStatusPanel"
	panel.position = Vector2(14, 14)
	panel.custom_minimum_size = Vector2(520, 0)
	var system_style := StyleBoxFlat.new()
	system_style.bg_color = Color(0.06, 0.08, 0.11, 0.90)
	system_style.set_corner_radius_all(6)
	system_style.set_content_margin_all(10)
	panel.add_theme_stylebox_override("panel", system_style)
	hud.add_child(panel)
	var contents := VBoxContainer.new()
	contents.custom_minimum_size = Vector2(500, 0)
	contents.add_theme_constant_override("separation", 4)
	panel.add_child(contents)
	var title := Label.new()
	title.text = "SINGLE-GALAXY  |  COLONY / DEPARTURES"
	title.add_theme_font_size_override("font_size", 17)
	contents.add_child(title)
	scope_label = Label.new()
	scope_label.text = "Connecting to original saved world..."
	contents.add_child(scope_label)
	native_label = Label.new()
	native_label.text = "COLONY — connecting to your saved world..."
	contents.add_child(native_label)
	ship_label = Label.new()
	ship_label.text = "Original ship not yet connected"
	contents.add_child(ship_label)
	status_label = Label.new()
	status_label.text = "COLONY > DOCK > SPACE | WASD on foot | E board | auto pilot | 3rd-person ship"
	status_label.add_theme_font_size_override("font_size", 12)
	contents.add_child(status_label)
	for line in [scope_label, native_label, ship_label, status_label]:
		line.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	retry_station_button = Button.new()
	retry_station_button.name = "RetryStationConnection"
	retry_station_button.text = "Retry connection to saved station"
	retry_station_button.visible = false
	retry_station_button.pressed.connect(_retry_original_station)
	contents.add_child(retry_station_button)

	flight_panel = PanelContainer.new()
	flight_panel.name = "FlightTelemetryPanel"
	flight_panel.set_anchors_and_offsets_preset(Control.PRESET_TOP_RIGHT)
	flight_panel.offset_left = -492.0
	flight_panel.offset_right = -14.0
	flight_panel.offset_top = 14.0
	flight_panel.visible = false # no live SHIP/ROCK until original C++ frames
	var flight_style := StyleBoxFlat.new()
	flight_style.bg_color = Color(0.07, 0.11, 0.15, 0.92)
	flight_style.set_corner_radius_all(6)
	flight_style.set_content_margin_all(10)
	flight_panel.add_theme_stylebox_override("panel", flight_style)
	hud.add_child(flight_panel)
	var flight_contents := VBoxContainer.new()
	flight_contents.custom_minimum_size = Vector2(458, 0)
	flight_contents.add_theme_constant_override("separation", 5)
	flight_panel.add_child(flight_contents)
	var flight_title := Label.new()
	flight_title.text = "FLIGHT TELEMETRY — ORIGINAL C++"
	flight_title.add_theme_font_size_override("font_size", 15)
	flight_contents.add_child(flight_title)
	flight_instrument_label = Label.new()
	flight_instrument_label.name = "ReadOnlyOriginalPilotInstrument"
	flight_instrument_label.text = "Awaiting verified original SHIP + ROCK receipts"
	flight_instrument_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	flight_contents.add_child(flight_instrument_label)
	asteroid_label = Label.new()
	asteroid_label.text = "No verified asteroid contact"
	asteroid_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	flight_contents.add_child(asteroid_label)

	fps_label = Label.new()
	fps_label.name = "FPSReadout"
	fps_label.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_LEFT)
	fps_label.offset_left = 15.0
	fps_label.offset_right = 310.0
	fps_label.offset_top = -31.0
	fps_label.offset_bottom = -8.0
	fps_label.add_theme_font_size_override("font_size", 14)
	fps_label.text = "FPS: measuring"
	hud.add_child(fps_label)
	var reticle := Label.new()
	reticle.text = "+"
	reticle.add_theme_font_size_override("font_size", 25)
	reticle.set_anchors_and_offsets_preset(Control.PRESET_CENTER)
	hud.add_child(reticle)
	# Read-only world-state action guidance; separate from top-left diagnostics.
	# Interaction never happens in this widget; E still flows through the
	# protected native person/ship command path and C++ authorization.
	var prompt := PanelContainer.new()
	prompt.name = "InteractionPrompt"
	prompt.set_anchors_preset(Control.PRESET_CENTER_BOTTOM)
	prompt.offset_left = -320.0
	prompt.offset_right = 320.0
	prompt.offset_top = -100.0
	prompt.offset_bottom = -42.0
	prompt.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var prompt_style := StyleBoxFlat.new()
	prompt_style.bg_color = Color(0.04, 0.09, 0.14, 0.94)
	prompt_style.border_color = Color(0.08, 0.89, 0.96, 0.9)
	prompt_style.set_border_width_all(2)
	prompt_style.set_corner_radius_all(8)
	prompt_style.set_content_margin_all(10.0)
	prompt.add_theme_stylebox_override("panel", prompt_style)
	hud.add_child(prompt)
	interaction_label = Label.new()
	interaction_label.name = "InteractionAction"
	interaction_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	interaction_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	interaction_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	interaction_label.add_theme_font_size_override("font_size", 18)
	interaction_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	interaction_label.text = "Connecting to saved station..."
	prompt.add_child(interaction_label)
	refinery_terminal = REFINERY_TERMINAL_UI.new()
	hud.add_child(refinery_terminal)

func _refresh_interaction_prompt() -> void:
	if not is_instance_valid(interaction_label):
		return
	var station_connected: bool = native_station_session != null and native_station_session.is_active()
	var ship_process_active: bool = native_player_session != null and native_player_session.is_active()
	# Launching a guardian is not proof of an admitted native cockpit POSE.
	var ship_connected: bool = ship_process_active and int(native_player_session._sequence) >= 0
	if ship_process_active and not ship_connected:
		interaction_label.text = "Connecting cockpit — waiting for verified original C++ ship pose"
		return
	if not _station_connect_attempted and not station_connected:
		interaction_label.text = "Connecting to saved station..."
		return
	if _station_connect_attempted and not station_connected and not ship_process_active and _handoff_target.is_empty():
		interaction_label.text = "Station unavailable — H to retry safely" if not _station_connection_failure.is_empty() else "Connecting to saved station..."
		return
	var person: Dictionary = {}
	if station_connected:
		person = native_station_session._person
	var seated: bool = is_instance_valid(player) and bool(player.native_seated)
	var display: String = INTERACTION_PROMPTS.hint(
		station_connected, person, ship_connected, seated,
		_last_native_dock, not _handoff_target.is_empty(),
		INTERACTION_PROMPTS.action_key("fps_inspect", "E"))
	if ship_connected and _colony_return_pending:
		display = "Exiting docked ship — verifying original seat release and SQLite handoff"
	elif ship_connected and _colony_return_armed and _last_native_dock == 1 and seated:
		display = "Press E to Disembark  |  Direct return to colony departure dock"
	if station_connected and _handoff_target.is_empty() and is_instance_valid(player) and not person.is_empty() and int(person.get("eligible_ship", 0)) > 0:
		var key_to_hatch: String = INTERACTION_PROMPTS.hatch_direction_key(person, player.global_transform.basis)
		if not key_to_hatch.is_empty():
			display += "  |  Hold " + key_to_hatch + " toward hatch"
	if interaction_label.text != display:
		interaction_label.text = display
	# The two-line flight/missing-miner explanation must not be clipped by
	# the single-line colony action banner height.
	var banner := interaction_label.get_parent() as PanelContainer
	if is_instance_valid(banner):
		var desired_top: float = -120.0 if display.contains("\n") else -100.0
		if banner.offset_top != desired_top:
			banner.offset_top = desired_top

func _on_inspected(display_name: String) -> void:
	# An incidental object-inspection ray must never replace the real
	# controls or the native startup/boarding error in the system HUD.
	if display_name == "No object in interaction range":
		return
	if not _station_connection_failure.is_empty():
		return
	_on_native_player_status("Visual inspection only: " + display_name)

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
			_on_native_player_status("Native writer handoff blocked: prior SGDX guardian still alive; retry after it exits")
			# Remain in native-authority mode: local Godot walking cannot silently
			# replace a live original C++ world while recovery is unresolved.
		return
	var destination := _handoff_target
	_handoff_target = ""
	_handoff_pid = -1
	if destination == "station":
		station_test_save_selected = true
		last_station_player_state = -1 # only fresh native PERSON can unlock P
		native_station_session.start()
	elif destination == "ship":
		var save_override := OS.get_user_data_dir().path_join("single_galaxy_station_test.sqlite") if station_test_save_selected else ""
		native_player_session.start(save_override)
	# If launching the replacement failed, do not keep an inert authority lock.
	if is_instance_valid(player) and not native_station_session.is_active() and not native_player_session.is_active():
		# Standalone never falls back to a fake freely walking local avatar
		# after failed native startup. In isolated script-only tests, the
		# original local-preview behavior remains explicit.
		player.use_native_authority(_player_session_autoconnect)

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_F11:
		# A deliberate operator action: capture only the game's Godot viewport,
		# never the Windows desktop or any other application.
		call_deferred("_capture_operator_view")
		get_viewport().set_input_as_handled()
		return
	if event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_V:
		# Explicitly non-toggleable: every native aboard view is third-person.
		_on_native_player_status("Pilot-only third-person camera is fixed while aboard")
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
		# Retry is for troubleshooting. H NEVER stops a healthy native writer
		# and is not part of the player's normal boarding/flight controls.
		_retry_original_station()
		get_viewport().set_input_as_handled()
		return
	if event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_P:
		if _player_session_autoconnect:
			_on_native_player_status("P developer stop is disabled in gameplay; dock and E to leave your ship")
			get_viewport().set_input_as_handled()
			return
		if native_player_session.is_active():
			native_player_session.stop("NATIVE FPS stopped by player; pose saved", true)
		else:
			if station_test_save_selected and last_station_player_state != 0:
				_on_native_player_status("Board your existing ship first; cockpit is unavailable while at station")
				get_viewport().set_input_as_handled()
				return
			var exiting_pid: int = native_station_session.process_id()
			if native_station_session.is_active():
				native_station_session.stop("Station writer closed before ship cockpit session", true)
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
	if is_instance_valid(retry_station_button) and connected:
		retry_station_button.visible = false
	if is_instance_valid(player):
		# Keep local unjournaled walking disabled during a protected role
		# handoff or while the native cockpit owns this saved world.
		player.use_native_authority(_player_session_autoconnect or connected or not _handoff_target.is_empty() or (native_player_session != null and native_player_session.is_active()))
	if is_instance_valid(scope_label):
		scope_label.text = "NATIVE STATION — WAITING FOR ORIGINAL PERSON" if connected else "STATION NATIVE DISCONNECTED"

func _on_native_station_status(message: String) -> void:
	# Expose a real persisted failure reason instead of "H didn't work".
	if message.begins_with("SGDX-SUPERVISED station:"):
		_station_connection_failure = ""
	elif message.contains("failed") or message.contains("exited") or message.contains("missing") or message.contains("timed out") or message.contains("still alive"):
		_station_connection_failure = message
	if is_instance_valid(retry_station_button):
		retry_station_button.visible = not _station_connection_failure.is_empty() and not native_station_session.is_active()
	_on_native_player_status(message)
	# An original C++ DENY must leave the terminal in a visible refused state.
	# Do not report success or synthesize a new lot while a host request fails.
	if is_instance_valid(refinery_terminal) and refinery_terminal.visible and (
		message.begins_with("Station action #") or
		message.begins_with("Station response timed out") or
		message.begins_with("Native station host exited")):
		refinery_terminal.report_error(message)

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

func _on_native_unload_requested() -> void:
	# No local inventory mutation or replacement mining mechanic.
	# The station C++ host rechecks physical proximity, actual owned docked
	# ship and one tonne of cargo before committing original SQLite.
	if native_station_session != null and native_station_session.is_active() and last_station_player_state == 4:
		native_station_session.unload_owned_ship_tonne()

func _hide_refinery_terminal() -> void:
	if is_instance_valid(refinery_terminal):
		refinery_terminal.visible = false
	if is_instance_valid(player) and get_window().has_focus():
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED

func _capture_operator_view() -> void:
	var directory := OS.get_environment("SG_FP3D_OPERATOR_CAPTURE_DIR")
	if directory.is_empty():
		_on_native_player_status("F11 viewport evidence requires the packaged Windows playtest launcher")
		return
	var station_authorized: bool = native_station_session != null and native_station_session.is_active() and last_station_player_state == 4
	var flight_authorized: bool = native_player_session != null and native_player_session.is_active()
	var joined: Dictionary = FLIGHT_INSTRUMENTS.from_verified_frames(_verified_flight_ship, _verified_flight_rock)
	if not station_authorized and (not flight_authorized or joined.is_empty()):
		_on_native_player_status("F11 refused: connect original H station or P ship and receive verified native frames")
		return
	if DirAccess.make_dir_recursive_absolute(directory) != OK:
		_on_native_player_status("F11 cannot create private playtest evidence directory")
		return
	await RenderingServer.frame_post_draw
	var image: Image = get_viewport().get_texture().get_image()
	if image == null or image.is_empty() or image.get_width() < 640 or image.get_height() < 360:
		_on_native_player_status("F11 capture failed: no usable rendered game viewport")
		return
	var id := str(Time.get_ticks_usec())
	var filename := "operator-" + id + ".png"
	var file_path := directory.path_join(filename)
	if image.save_png(file_path) != OK:
		_on_native_player_status("F11 failed to save rendered viewport")
		return
	var receipt := {
		"schema":"SG-FP3D-OPERATOR-01", "source":"explicit-F11-Godot-viewport",
		"wall_time_unix":Time.get_unix_time_from_system(),
		"image":filename, "image_sha256":FileAccess.get_sha256(file_path),
		"image_width":image.get_width(), "image_height":image.get_height(),
		"display_backend":DisplayServer.get_name(),
		"native_flight_joined":not joined.is_empty(),
		"station_person":native_station_session._person.duplicate(true) if station_authorized else {},
		"original_ship":_verified_flight_ship.duplicate(true) if not joined.is_empty() else {},
		"original_rock":_verified_flight_rock.duplicate(true) if not joined.is_empty() else {},
		"original_target_instrument":joined,
		"exterior_camera":is_instance_valid(exterior_camera) and exterior_camera.current,
		"person_camera":is_instance_valid(player) and is_instance_valid(player.camera) and player.camera.current,
		"operator_visual_judgment":"NOT YET RECORDED"
	}
	var log_path := directory.path_join("operator-viewport-receipts.jsonl")
	var f := FileAccess.open(log_path, FileAccess.READ_WRITE)
	if f == null:
		f = FileAccess.open(log_path, FileAccess.WRITE)
	if f == null:
		_on_native_player_status("F11 screenshot saved, but native receipt could not be written")
		return
	f.seek_end()
	f.store_line(JSON.stringify(receipt))
	f.close()
	_on_native_player_status("F11 original-world viewport captured: " + filename)

func _record_native_projection(kind: String, evidence: Dictionary) -> void:
	# CLIENT-OBSERVED diagnostics only, not ship controls or a second save.
	_native_visual_evidence_buffer.append(JSON.stringify({
		"source":"godot-observed-native-frame",
		"event":kind,"wall_unix":Time.get_unix_time_from_system(),"frame":evidence}))
	if _native_visual_evidence_buffer.size() >= NATIVE_EVIDENCE_BUFFER_LIMIT:
		_flush_native_visual_evidence()

func _flush_native_visual_evidence() -> void:
	if _native_visual_evidence_buffer.is_empty():
		return
	# One bounded synchronous disk write per group, not one per SHIP/ROCK
	# callback. Losing a diagnostic must never create a gameplay authority.
	var path := "user://sg_station_visual_acceptance.jsonl"
	var f := FileAccess.open(path, FileAccess.READ_WRITE)
	if f == null:
		f = FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		_native_visual_evidence_buffer.clear()
		return
	if f.get_length() > 1048576:
		f.close()
		f = FileAccess.open(path, FileAccess.WRITE)
		if f == null:
			_native_visual_evidence_buffer.clear()
			return
	f.seek_end()
	for entry in _native_visual_evidence_buffer:
		f.store_line(entry)
	f.close()
	_native_visual_evidence_buffer.clear()
	_last_visual_evidence_flush_ms = Time.get_ticks_msec()

func _on_native_station_person(person: Dictionary) -> void:
	_record_native_projection("PERSON", person)
	last_station_player_state = int(person["state"])
	_station_connection_failure = ""
	if is_instance_valid(retry_station_button):
		retry_station_button.visible = false
	if is_instance_valid(scope_label):
		scope_label.text = "STATION CONNECTED — ORIGINAL C++ / SQLITE" if last_station_player_state == 4 else "BOARDING CONFIRMED — ORIGINAL C++ / SQLITE"
	if last_station_player_state != 4:
		_hide_refinery_terminal()
	_show_native_location(last_station_player_state == 4)
	if is_instance_valid(player):
		player.use_native_ship_custody(last_station_player_state == 0)
		player.use_native_seat(false)
		player.global_position = Vector3(float(person["x_mm"]) / 1000.0, AVATAR_CENTER_HEIGHT_M,
			float(person["z_mm"]) / 1000.0)
	if is_instance_valid(ship_label):
		ship_label.text = "STATION #%d | %s | available owned ship #%d | position (%.2f, %.2f)m" % [
			int(person["station"]), "ON FOOT" if int(person["state"]) == 4 else "ABOARD SHIP",
			int(person["eligible_ship"]), float(person["x_mm"])/1000.0,
			float(person["z_mm"])/1000.0]
	if last_station_player_state == 4 and _colony_return_pending:
		# Only the original EXIT receipt permits returning to colony walking.
		_colony_return_pending = false
		_colony_return_armed = false
	if last_station_player_state == 0 and _colony_return_pending:
		# This PERSON is a live C++ aboard receipt, not a new BOARD. Submit
		# one original EXIT through the station host after its genesis frame.
		call_deferred("_exit_docked_ship_after_verified_station_person")
	if last_station_player_state == 0 and _player_session_autoconnect and not _colony_return_pending:
		# A genuine saved BOARD (also on restart) must automatically open
		# the cockpit, never demand another developer-only P key.
		call_deferred("_enter_cockpit_after_verified_board")

func _on_native_player_connection(connected: bool) -> void:
	# Process start alone is NOT proof of ship custody; wait for POSE.
	if connected:
		_pilot_auto_seat_attempted = false
	if not connected:
		_verified_flight_ship.clear()
		_verified_flight_rock.clear()
		if is_instance_valid(flight_instrument_label):
			flight_instrument_label.text = "FLIGHT SENSOR: offline; previous native contacts are no longer live"
	if not connected:
		_external_requested = false
		if is_instance_valid(native_asteroid_mesh):
			native_asteroid_mesh.visible = false
		# If a known native aboard writer failed, leave the third-person ship
		# view visible but disable input rather than exposing a fake walkable
		# cabin. Only a fresh original PERSON can return to colony walking.
		_show_native_location(last_station_player_state == 4)
	if is_instance_valid(player):
		player.use_native_authority(_player_session_autoconnect or connected or (native_station_session != null and native_station_session.is_active()) or not _handoff_target.is_empty())
	if not connected and is_instance_valid(ship_label):
		ship_label.text = "SHIP: native host disconnected; previous readout not live"
	if is_instance_valid(scope_label):
		scope_label.text = "NATIVE IN-SHIP POSE + ORIGINAL C++ FLIGHT" if connected else "Native cockpit unavailable — original saved world retained"

func _refresh_native_flight_instruments() -> void:
	if not is_instance_valid(flight_instrument_label):
		return
	# A new SHIP tick invalidates the prior rock until this very tick has a
	# corresponding, original C++ ROCK/ROCKNONE observation.
	var projection: Dictionary = FLIGHT_INSTRUMENTS.from_verified_frames(
		_verified_flight_ship, _verified_flight_rock)
	flight_instrument_label.text = FLIGHT_INSTRUMENTS.display(projection)

func _on_native_ship_intents(actions: PackedStringArray) -> void:
	if native_player_session != null:
		native_player_session.submit_flight(actions)

func _on_native_ship_state(ship: Dictionary) -> void:
	_record_native_projection("SHIP", ship)
	_verified_flight_ship = ship.duplicate(true)
	_verified_flight_rock.clear()
	_refresh_native_flight_instruments()
	if not is_instance_valid(ship_label):
		return
	var states := ["IN SPACE", "DOCKED", "UNDOCKING", "DOCKING"]
	var dock: int = int(ship["dock"])
	if _last_native_dock == 0 and dock == 1 and last_station_player_state == 0:
		_colony_return_armed = true # verified C++ return docking, not client input
	if dock == 0:
		_colony_return_armed = false
	_last_native_dock = dock
	if is_instance_valid(player):
		player.use_native_dock_state(dock)
	if is_instance_valid(exterior_ship_visual):
		# Native state remains exact for the collision/camera receipt tests.
		# Keep the current camera pose through discrete C++ yaw steps, then
		# smoothly restore the camera's own local mount each display frame.
		var smooth_camera: bool = is_instance_valid(exterior_camera) and exterior_camera.current
		var camera_before := exterior_camera.global_transform if smooth_camera else Transform3D.IDENTITY
		exterior_ship_visual.rotation.y = native_heading_to_godot_yaw(int(ship["heading"]))
		if smooth_camera:
			exterior_camera.global_transform = camera_before
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
	_verified_flight_rock = contact.duplicate(true)
	_refresh_native_flight_instruments()
	if not is_instance_valid(native_asteroid_mesh):
		return
	var found := bool(contact["found"])
	native_asteroid_mesh.visible = found
	if found:
		# Original world X/Y/Z -> Godot horizontal X/Z and vertical Y.
		# Relative render projection stays within 30 metres of native ship.
		native_asteroid_mesh.position = native_rock_visual_offset(contact)
		var visual_radius := maxf(0.3, float(contact["radius_mm"]) / 1000.0)
		native_asteroid_mesh.scale = Vector3.ONE * visual_radius
		var range_m := Vector3(float(contact["dx_mm"]),float(contact["dy_mm"]),float(contact["dz_mm"])).length() / 1000.0
		asteroid_label.text = "ROCK #%d | %.1fm | %d t finite ore | ship cargo %d/%d t | mining tool: %s | PILOT 3RD-PERSON" % [
			int(contact["rock"]),range_m,int(contact["ore_tonnes"]),int(contact["cargo_tonnes"]),
			int(contact["cargo_capacity"]),"ONLINE" if int(contact["mining_tool_online"]) == 1 else "NOT FITTED"]
	else:
		asteroid_label.text = "No original asteroid within 30m | ship cargo %d/%d t | mining tool: %s" % [
			int(contact["cargo_tonnes"]),int(contact["cargo_capacity"]),
			"ONLINE" if int(contact["mining_tool_online"]) == 1 else "NOT FITTED"]
	if int(contact.get("impact_rock", 0)) > 0:
		asteroid_label.text += " | ORIGINAL SOLID CONTACT: asteroid #%d at tick %d (velocity stopped)" % [
			int(contact["impact_rock"]), int(contact["impact_tick"])]

func _on_native_player_seat_intent() -> void:
	# E cannot unexpectedly trigger station BOARD while the player is working
	# with the visible refinery overlay; the world still remains C++-owned.
	if is_instance_valid(refinery_terminal) and refinery_terminal.visible:
		return
	if native_station_session != null and native_station_session.is_active():
		native_station_session.interact_hangar()
	elif native_player_session != null and native_player_session.is_active():
		if _colony_return_pending:
			return # one original EXIT transaction already in progress
		if _colony_return_armed and _last_native_dock == 1 and \
				native_player_session._seated and native_player_session._pending == 0:
			# E is DISembark only. Internally obtain one native seat-release
			# receipt, then close cockpit writer and let native station EXIT
			# authorize the on-foot colony. Never show cabin walk in between.
			_colony_return_pending = true
			native_player_session.toggle_seat()
		elif _last_native_dock == 0:
			_on_native_player_status("Pilot seat locked in space; return to the colony and dock first")
		else:
			_on_native_player_status("Pilot-only ship: U to depart; after returning and docking, E to disembark")

func _on_native_player_intent(heading: Vector3) -> void:
	if native_station_session != null and native_station_session.is_active():
		native_station_session.submit_walk(heading)
	# PILOT-ONLY: intentionally NEVER route cabin MOVE to native cockpit,
	# even when a stale/extra Godot emitter calls this handler directly.
	# The only in-ship user controls are authorized original FLIGHT verbs.

func _on_native_player_pose(pose: Dictionary) -> void:
	_record_native_projection("POSE", pose)
	# Use ONLY the acknowledged native SQLite pose. Even if the original
	# seat-release receipt says ON FOOT during authorized disembark, the
	# ship stays third-person and no cabin walking is exposed.
	if is_instance_valid(player):
		player.use_native_ship_custody(true)
		player.use_native_seat(bool(pose["seated"]))
		player.global_position = Vector3(float(pose["x_mm"]) / 1000.0,
			AVATAR_CENTER_HEIGHT_M, float(pose["z_mm"]) / 1000.0)
	_show_native_location(false)
	if _colony_return_pending and not bool(pose["seated"]) and _last_native_dock == 1:
		call_deferred("_complete_docked_disembark_after_native_seat_release")

func _on_native_player_status(message: String) -> void:
	if is_instance_valid(native_label):
		# The real native SEAT=0 acknowledgement is an invisible stage of a
		# direct docked EXIT. Never advertise free cabin walking during handoff.
		if _colony_return_pending and message.begins_with("NATIVE ON FOOT"):
			native_label.text = "DOCKED EXIT — original C++ seat release verified, transferring to colony"
		else:
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
	_flush_native_visual_evidence()
	if native_session != null:
		native_session.stop("Native client closed")
	if native_player_session != null:
		native_player_session.stop("Native FPS client closed")
	if native_station_session != null:
		native_station_session.stop("Native station client closed")

func _process(delta: float) -> void:
	# VISUAL ONLY: interpolate the spectator camera, not the original-world
	# ship or camera authority. UI 60 FPS is not native C++ transaction rate.
	if is_instance_valid(exterior_camera) and exterior_camera.current:
		exterior_camera.transform = exterior_camera.transform.interpolate_with(
			_exterior_camera_rest, clampf(delta * 12.0, 0.0, 1.0))
	_advance_native_handoff()
	if native_session != null:
		native_session.poll()
	if native_player_session != null:
		native_player_session.poll()
	if native_station_session != null:
		native_station_session.poll()
	_ensure_native_pilot_role()
	_refresh_interaction_prompt()
	if not _native_visual_evidence_buffer.is_empty() and \
			Time.get_ticks_msec() - _last_visual_evidence_flush_ms >= NATIVE_EVIDENCE_FLUSH_INTERVAL_MS:
		_flush_native_visual_evidence()

	_fps_accumulator += delta
	if _fps_accumulator >= 0.5 and is_instance_valid(fps_label):
		fps_label.text = "FPS: %d  |  Phys: 60Hz" % Engine.get_frames_per_second()
		_fps_accumulator = 0.0
