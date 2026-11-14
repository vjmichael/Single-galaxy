extends SceneTree
## FP3D-COLONY-01 — real Godot 4.7.2 colony -> dock -> spacecraft
## visual/interaction acceptance. Synthetic PERSON frames are clearly UI
## fixtures only; they do not grant boarding or mutate the native SQLite.
const PROMPTS := preload("res://world/interaction_prompts.gd")
const LAYOUT := preload("res://world/colony_dock_layout.gd")

func _initialize() -> void:
	call_deferred("_run")

func _fail(message: String) -> void:
	push_error("FP3D-COLONY-01 Godot dock acceptance FAIL: " + message)
	quit(1)

func _run() -> void:
	var scene := load("res://world/main.tscn") as PackedScene
	if scene == null:
		_fail("main colony game scene unavailable")
		return
	var stage := scene.instantiate()
	root.add_child(stage)
	await process_frame
	var station := stage.get_node_or_null("StationInterior_Prototype") as Node3D
	if station == null:
		_fail("original authoritative station scene root missing")
		return
	var colony := station.get_node_or_null("ColonyArrival") as Node3D
	var walkway := station.get_node_or_null("ColonyToDockWalkway") as Node3D
	var dock := station.get_node_or_null("SpacefaringDock") as Node3D
	if colony == null or walkway == null or dock == null:
		_fail("missing one of the distinct COLONY / WALKWAY / SPACEFARING DOCK regions")
		return
	if colony.get_child_count() < 10 or dock.get_child_count() < 10 or walkway.get_child_count() < 5:
		_fail("colony services, dock markings or pedestrian route not constructed")
		return
	for area in [colony, walkway, dock]:
		if not area.find_children("*", "CollisionObject3D", true, false).is_empty():
			_fail("new colony/dock scenery created a fake Godot obstacle absent in native C++")
			return
	var original_spawn := colony.get_node_or_null("Original native person colony spawn emblem") as MeshInstance3D
	var colony_sign := colony.get_node_or_null("Colony arrival wayfinding") as Label3D
	var dock_sign := dock.get_node_or_null("Departure dock label") as Label3D
	var route_sign := walkway.get_node_or_null("Pedestrian dock threshold") as Label3D
	if original_spawn == null or colony_sign == null or dock_sign == null or route_sign == null:
		_fail("colony spawn, ship departures or colony-dock wayfinding is invisible")
		return
	if original_spawn.position.distance_to(Vector3(1.6,0.086,0.54)) > 0.001:
		_fail("colony spawn scene has diverged from original C++ PERSON genesis (1600,0)mm")
		return
	if not colony_sign.text.contains("COLONY ARRIVALS") or not dock_sign.text.contains("TO SPACE") or not route_sign.text.contains("DOCK CHECKPOINT"):
		_fail("colony-to-space departure signs do not identify the transition")
		return
	var embark := dock.get_node_or_null("Ship embarkation ramp") as MeshInstance3D
	var berth := dock.get_node_or_null("Dock charcoal deck") as MeshInstance3D
	if embark == null or berth == null or absf(embark.position.z + 1.9) > 0.001:
		_fail("dock's visible boarding ramp does not lead to the original (0,0) hatch")
		return
	var nose := dock.get_node_or_null("Owned ship nose highlight") as MeshInstance3D
	var canopy := dock.get_node_or_null("Owned ship dark cockpit canopy") as MeshInstance3D
	if nose == null or canopy == null:
		_fail("white ship lacks a visible spacecraft nose/canopy identity")
		return
	var hull := station.get_node_or_null("Docked owned ship hull (visual)") as StaticBody3D
	if hull == null:
		_fail("no original white docked spacecraft")
		return
	var ship_mesh := hull.get_child(0) as MeshInstance3D
	if ship_mesh == null:
		_fail("ship has no actual visible mesh")
		return
	var ship_color := ship_mesh.material_override as StandardMaterial3D
	if hull.position.distance_to(Vector3(0.0,1.34,-4.9)) > 0.001 or ship_color == null or ship_color.albedo_color.r < 0.9:
		_fail("white owned spacecraft not docked near the pedestrian embarkation bridge")
		return
	var deck_color := berth.material_override as StandardMaterial3D
	if deck_color == null or deck_color.albedo_color.r > 0.4:
		_fail("space-faring dock deck must be dark gray, distinct from the white ship")
		return
	var person := stage.get_node_or_null("LocalGrayboxPerson_NOT_NATIVE_PLAYER") as CharacterBody3D
	if person == null:
		_fail("player viewport was not created in the colony scene")
		return
	# Admitted original PERSON positions project onto the new colony,
	# never a client-side pretend PERSON or another SQLite writer.
	stage.call("_on_native_station_person", {"state":4,"station":1,"ship":0,
		"eligible_ship":2,"x_mm":1600,"z_mm":0})
	if absf(person.global_position.x - 1.6) > 0.001 or absf(person.global_position.z) > 0.001:
		_fail("player did not spawn at original colony position")
		return
	var approach: String = PROMPTS.hint(true,{"state":4,"x_mm":1600,
		"z_mm":0,"eligible_ship":2},false,false,1,false,"E")
	if not approach.contains("COLONY") or approach.contains("Press E to Board"):
		_fail("colony walkway incorrectly grants BOARD before native dock threshold")
		return
	var ready: String = PROMPTS.hint(true,{"state":4,"x_mm":0,
		"z_mm":0,"eligible_ship":2},false,false,1,false,"E")
	if not ready.contains("Press E to Board"):
		_fail("owned docked ship does not display exact Press E to Board prompt at dock")
		return
	var absent: String = PROMPTS.hint(true,{"state":4,"x_mm":0,
		"z_mm":0,"eligible_ship":0},false,false,1,false,"E")
	if absent.contains("Press E to Board"):
		_fail("no owned ship, but dock fraudulently promises boarding")
		return
	stage.call("_on_native_station_person", {"state":0,"station":1,"ship":2,
		"eligible_ship":2,"x_mm":0,"z_mm":0})
	if station.visible:
		_fail("verified native BOARD did not leave colony view and enter spacecraft")
		return
	stage.queue_free()
	print("FP3D-COLONY-01 REAL GODOT PASS: original colony spawn, gray dock / white identifiable ship, walkable visual-only departure route, exact native E boarding prompt, verified transition to spaceship")
	quit(0)
