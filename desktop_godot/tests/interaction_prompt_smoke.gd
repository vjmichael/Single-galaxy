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
	if text == null or not text.text.contains("Press H") or not text.autowrap_mode == TextServer.AUTOWRAP_WORD_SMART:
		_fail("unauthenticated player not directed toward H native connection")
		return
	var hatch := stage.get_node_or_null("StationInterior_Prototype/Station_Hangar_Entry_At_Zero") as MeshInstance3D
	if hatch == null or hatch.material_override == null or hatch.material_override.albedo_color.b < 0.9 or absf(hatch.position.z) > 0.001:
		_fail("bright non-colliding original (0,0) hatch marker not visible")
		return
	if hatch is CollisionObject3D:
		_fail("visual-only boarding hatch obtained unauthorized physics identity")
		return
	if PROMPTS.action_key("fps_inspect", "E") != "E":
		_fail("HUD prompt action key does not match live InputMap E")
		return
	var near: Dictionary = {"state":4,"x_mm":600,"z_mm":-600,"eligible_ship":2}
	var ready: String = PROMPTS.hint(true,near,false,false,-1,false,"E")
	if not ready.contains("Press E") or not ready.contains("ship #2"):
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
	if not PROMPTS.hint(true,aboard,false,false,1,false,"E").contains("Press P"):
		_fail("native boarded PERSON not offered cockpit handoff")
		return
	if not PROMPTS.hint(false,{},true,false,1,false,"E").contains("press E"):
		_fail("on-foot native cockpit not offered seat request")
		return
	if not PROMPTS.hint(false,{},true,true,1,false,"E").contains("Press U"):
		_fail("seated original docked ship not offered undock key")
		return
	if not PROMPTS.hint(false,{},true,true,0,false,"E").contains("V"):
		_fail("undocked native ship not shown exterior controls")
		return
	if PROMPTS.hint(true,near,false,false,1,true,"E").contains("Press E"):
		_fail("interrupted SQLite writer handoff still offered active boarding")
		return
	stage.queue_free()
	print("FP3D-UX-01 real Godot interaction prompt PASS: bright hatch, dynamic InputMap E, exact original C++ 600mm boarding/1200mm refinery, safe native source-bound guidance; synthetic UI fixture only")
	quit(0)
