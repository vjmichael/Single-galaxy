extends SceneTree
## FP3D-UX-01: real Godot 4.7.2 UI/scene test of READ-ONLY native
## interaction hints. PERSON fixtures are synthetic for UI only: not
## acceptance of authorization, the original C++ server or ship control.
const PROMPTS := preload("res://world/interaction_prompts.gd")
const PILOT_CONTROLLER := preload("res://world/player_controller.gd")

func _initialize() -> void:
	call_deferred("_run")

func _fail(message: String) -> void:
	push_error("FP3D-UX-01 original-world interaction prompt FAIL: " + message)
	quit(1)

func _run() -> void:
	var scene := load("res://world/main.tscn") as PackedScene
	if scene == null:
		_fail("Godot scene not loadable")
		return
	var stage := scene.instantiate()
	root.add_child(stage)
	await process_frame
	var prompt := stage.find_child("InteractionPrompt", true, false) as PanelContainer
	if prompt == null or not prompt.visible or prompt.mouse_filter != Control.MOUSE_FILTER_IGNORE:
		_fail("separate, non-clickable action banner missing from real game")
		return
	var text := prompt.get_node_or_null("InteractionAction") as Label
	if text == null or not text.text.contains("Connecting to saved station") or text.autowrap_mode != TextServer.AUTOWRAP_WORD_SMART:
		_fail("standalone HUD incorrectly demands developer-only H on initial game start")
		return
	var retry := stage.find_child("RetryStationConnection", true, false) as Button
	if retry == null or retry.visible or not retry.text.contains("Retry"):
		_fail("safe explicit station retry must stay hidden until an actual native failure")
		return
	var hatch := stage.get_node_or_null("StationInterior_Prototype/Station_Hangar_Entry_At_Zero") as MeshInstance3D
	if hatch == null:
		_fail("original (0,0) hatch mesh not visible")
		return
	var accent := hatch.material_override as StandardMaterial3D
	if accent == null or accent.albedo_color.b < 0.9 or absf(hatch.position.z) > 0.001 or hatch.position.y < 0.065:
		_fail("cyan hatch marker still hidden under station gangway surface")
		return
	var sign := stage.get_node_or_null("StationInterior_Prototype/BoardingHatch_WorldSign_VisualOnly") as Label3D
	if sign == null or not sign.text.contains("BOARDING HATCH") or sign.position.y < 1.5:
		_fail("native (0,0) hatch lacks elevated legible world-space sign")
		return
	var station_floor := stage.get_node_or_null("StationInterior_Prototype/Station floor") as StaticBody3D
	var ship_hull := stage.get_node_or_null("StationInterior_Prototype/Docked owned ship hull (visual)") as StaticBody3D
	if station_floor == null or ship_hull == null:
		_fail("station and original owned ship visual geometry missing")
		return
	var gray_mesh := station_floor.get_child(0) as MeshInstance3D
	var white_mesh := ship_hull.get_child(0) as MeshInstance3D
	if gray_mesh == null or white_mesh == null:
		_fail("original station or ship geometry lacks colored mesh")
		return
	var gray_mat := gray_mesh.material_override as StandardMaterial3D
	var white_mat := white_mesh.material_override as StandardMaterial3D
	if gray_mat == null or white_mat == null or gray_mat.albedo_color.r > 0.6 or white_mat.albedo_color.r < 0.9:
		_fail("station must be medium gray and original ship clearly white")
		return
	var system := stage.find_child("SystemStatusPanel", true, false) as PanelContainer
	var flight := stage.find_child("FlightTelemetryPanel", true, false) as PanelContainer
	var fps := stage.find_child("FPSReadout", true, false) as Label
	if system == null or flight == null or fps == null or flight.visible or fps.anchor_top < 0.9:
		_fail("HUD is not segmented into compact status, hidden idle flight and bottom FPS")
		return
	if PROMPTS.action_key("fps_inspect", "E") != "E":
		_fail("HUD prompt action key does not match live InputMap E")
		return
	# The creator's initial original station spawn is x=1600mm. With the
	# default yaw the next intuitive move is A toward the cyan (0,0) tile.
	var spawn: Dictionary = {"state":4,"x_mm":1600,"z_mm":0,"eligible_ship":2}
	if PROMPTS.hatch_direction_key(spawn, Basis.IDENTITY) != "A":
		_fail("initial native station spawn lacks camera-relative A hatch guidance")
		return
	if PROMPTS.hatch_direction_key(spawn, Basis(Vector3.UP, PI / 2.0)) != "W":
		_fail("camera-turn no longer updates boarding guidance to actual WASD")
		return
	var near: Dictionary = {"state":4,"x_mm":600,"z_mm":-600,"eligible_ship":2}
	if PROMPTS.hatch_direction_key(near, Basis.IDENTITY) != "":
		_fail("WASD guidance did not clear at native boarding threshold")
		return
	var ready: String = PROMPTS.hint(true,near,false,false,-1,false,"E")
	if not ready.contains("Press E to Board") or not ready.contains("ship #2"):
		_fail("authorized PERSON at the exact 600mm square boundary lacks E boarding guidance")
		return
	var far: Dictionary = {"state":4,"x_mm":601,"z_mm":-600,"eligible_ship":2}
	if PROMPTS.hint(true,far,false,false,-1,false,"E").contains("Press E"):
		_fail("client claimed E boarding availability OUTSIDE original C++ hatch bound")
		return
	if PROMPTS.hint(false,near,false,false,-1,false,"E").contains("Board owned"):
		_fail("synthetic or historical PERSON granted boarding without active native session")
		return
	var no_ship: Dictionary = {"state":4,"x_mm":0,"z_mm":0,"eligible_ship":0}
	if PROMPTS.hint(true,no_ship,false,false,-1,false,"E").contains("Press E"):
		_fail("no eligible original ship but UI offered E boarding")
		return
	var terminal: Dictionary = {"state":4,"x_mm":3000,"z_mm":0,"eligible_ship":2}
	if not PROMPTS.hint(true,terminal,false,false,-1,false,"E").contains("Press R"):
		_fail("real 1200mm native refinery accessibility did not show R")
		return
	var aboard: Dictionary = {"state":0,"x_mm":0,"z_mm":0,"eligible_ship":2}
	if not PROMPTS.hint(true,aboard,false,false,1,false,"E").contains("opening your ship cockpit"):
		_fail("native boarded PERSON not offered cockpit handoff")
		return
	if not PROMPTS.hint(false,{},true,false,1,false,"E").contains("Automatic pilot seating"):
		_fail("a boarded player was incorrectly offered a cabin walk/seat toggle")
		return
	if not PROMPTS.hint(false,{},true,true,1,false,"E").contains("Press U to Undock"):
		_fail("seated original docked ship not offered undock key")
		return
	if not PROMPTS.hint(false,{},true,true,0,false,"E").contains("THIRD-PERSON FLIGHT") or \
			PROMPTS.hint(false,{},true,true,0,false,"E").contains("V exterior"):
		_fail("the seated native ship did not advertise mandatory third-person view")
		return
	if PROMPTS.hint(true,near,false,false,1,true,"E").contains("Press E"):
		_fail("interrupted SQLite writer handoff still offered active boarding")
		return
	# FP3D-UX04: engine-constructed visual-only orientation stars are fixed
	# in the viewer's inertial reference, not real C++ asteroid contacts.
	var bearings := stage.get_node_or_null(
		"NativeAsteroidExternalView/VisualOnlyBearingDots_NOT_NativeContacts") as MultiMeshInstance3D
	if bearings == null or bearings.multimesh == null or bearings.multimesh.instance_count != 20 or \
			not bearings.find_children("*", "CollisionObject3D", true, false).is_empty():
		_fail("native ship has no non-colliding independent yaw reference when no rock is in sensor")
		return
	if PILOT_CONTROLLER.native_mouse_yaw_direction(5.0) != 1 or \
			PILOT_CONTROLLER.native_mouse_yaw_direction(-5.0) != -1 or \
			PILOT_CONTROLLER.native_mouse_yaw_direction(0.5) != 0:
		_fail("mouse steer must map to bounded native RIGHT/LEFT intent, not client camera yaw")
		return
	var pilot = stage.find_child("LocalGrayboxPerson_NOT_NATIVE_PLAYER", true, false)
	if pilot == null:
		_fail("no original native-action-only cockpit controller")
		return
	pilot.use_native_dock_state(0)
	if pilot.native_dock_state != 0 or not PROMPTS.hint(false,{},true,true,0,false,"E").contains("Mouse/A,D turn") or \
			not PROMPTS.hint(false,{},true,true,0,false,"E").contains("MINING LOCKED"):
		_fail("native cockpit missing discoverable mouse steering or truthful not-fitted mining gate")
		return
	# R9700 physical failure regression: a released mouse cursor must NEVER
	# suppress focused keyboard thrust/yaw on a verified seated ship. These
	# are synthetic Input actions to test Godot intent only, not C++ physics.
	pilot.use_native_authority(true)
	pilot.use_native_ship_custody(true)
	pilot.use_native_seat(true)
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	Input.action_press("ship_thrust_forward")
	Input.action_press("ship_yaw_left")
	Input.action_press("ship_undock")
	var free_cursor_actions: PackedStringArray = pilot._collect_native_pilot_actions()
	if not free_cursor_actions.has("FWD") or not free_cursor_actions.has("LEFT") or free_cursor_actions.has("UND"):
		_fail("released mouse cursor suppressed native keyboard thrust/yaw or bypassed in-space undock gate")
		return
	pilot.use_native_dock_state(1)
	Input.action_press("ship_dock")
	var docked_actions: PackedStringArray = pilot._collect_native_pilot_actions()
	if not docked_actions.has("UND") or docked_actions.has("DCK"):
		_fail("docked keyboard U was suppressed or DCK bypassed original dock-state gating")
		return
	for action_name in ["ship_thrust_forward", "ship_yaw_left", "ship_undock", "ship_dock"]:
		Input.action_release(action_name)
	pilot.use_native_seat(false)
	Input.action_press("ship_thrust_forward")
	if not pilot._collect_native_pilot_actions().is_empty():
		_fail("unseated player generated forbidden original flight intent")
		return
	pilot.use_native_seat(true)
	pilot.use_native_ship_custody(false)
	if not pilot._collect_native_pilot_actions().is_empty():
		_fail("on-foot station player generated forbidden ship thrust intent")
		return
	Input.action_release("ship_thrust_forward")
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	print("FP3D-UX-05 REAL GODOT INTENT PASS: cursor released keyboard FWD/LEFT, dock/undock eligibility and fail-closed original seat/ship custody")
	# Static engine-only visual test: never fabricate a C++ aboard receipt.
	# The genuine SGDX/PERSON/POSE round-trip is covered separately.
	stage._external_requested = false
	stage._show_native_location(false)
	pilot.use_native_ship_custody(true)
	if not stage.space_preview.visible or stage.ship_space.visible or \
			not stage.exterior_camera.current or pilot.camera.current or not pilot.native_in_ship:
		_fail("docked ship falsely showed an explorable cabin or a first-person camera")
		return
	stage._last_native_dock = 0
	stage._show_native_location(false)
	if not stage.exterior_camera.current or pilot.camera.current or \
			not stage.space_preview.visible or stage.ship_space.visible:
		_fail("in-space ship camera failed permanent third-person rule")
		return
	stage._show_native_location(true)
	pilot.use_native_ship_custody(false)
	if not pilot.camera.current or stage.space_preview.visible or \
			not stage.station_space.visible or pilot.native_in_ship:
		_fail("leaving ship did not restore colony first-person camera/custody")
		return
	stage.queue_free()
	print("FP3D-PILOT-01 REAL GODOT VISUAL PASS: docked and flying always third-person, no cabin, colony restores first-person")
	print("FP3D-UX-04 REAL GODOT PILOT UX PASS: native mouse-turn intent, inertial visual-only yaw markers, held-dock affordance and truthful no-miner guidance")
	print("FP3D-UX-02 real Godot visual/input PASS: visible cyan boarding tile + beacon above gangway, gray station/white hull, separate HUD, E prompts match native 600mm hatch/1200mm refinery")
	quit(0)
