extends SceneTree
## FP3D-UX-01: real Godot 4.7.2 UI/scene test of READ-ONLY native
## interaction hints. PERSON fixtures are synthetic for UI only: not
## acceptance of authorization, the original C++ server or ship control.
const PROMPTS := preload("res://world/interaction_prompts.gd")

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
	if not PROMPTS.hint(false,{},true,false,1,false,"E").contains("Press E to Sit"):
		_fail("on-foot native cockpit not offered seat request")
		return
	if not PROMPTS.hint(false,{},true,true,1,false,"E").contains("Press U to Undock"):
		_fail("seated original docked ship not offered undock key")
		return
	if not PROMPTS.hint(false,{},true,true,0,false,"E").contains("V"):
		_fail("undocked native ship not shown exterior controls")
		return
	if PROMPTS.hint(true,near,false,false,1,true,"E").contains("Press E"):
		_fail("interrupted SQLite writer handoff still offered active boarding")
		return
	stage.queue_free()
	print("FP3D-UX-02 real Godot visual/input PASS: visible cyan boarding tile + beacon above gangway, gray station/white hull, separate HUD, E prompts match native 600mm hatch/1200mm refinery")
	quit(0)
