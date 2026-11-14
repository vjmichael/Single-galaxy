extends SceneTree
## VIS-01: live Godot 4.7.2 procedural mesh/material/viewport policy smoke.
## Synthetic read-only frames are NEVER accepted as original game movement.
const VIS := preload("res://world/visual_quality_profile.gd")
func _initialize() -> void:
	call_deferred("_run")
func _fail(message: String) -> void:
	push_error("SG-VIS01 FAIL: " + message)
	quit(1)
func _run() -> void:
	var scene := load("res://world/main.tscn") as PackedScene
	if scene == null:
		_fail("cannot instantiate real Godot game scene")
		return
	var stage := scene.instantiate()
	root.add_child(stage)
	await process_frame
	var exterior := stage.get_node_or_null("NativeAsteroidExternalView/OriginalShip_VisualHullOnly") as Node3D
	if exterior == null or exterior.get_child_count() != 4:
		_fail("original hull / two wings / camera identity changed")
		return
	var body := exterior.get_child(0) as Node3D
	if body == null or body.get_node_or_null("Tapered_1977_IndustrialPressureHull") == null or \
			body.get_node_or_null("Recessed dark cockpit windshield") == null or \
			body.get_node_or_null("Drive exhaust core -1") == null:
		_fail("retro-industrial ship mesh detail incomplete")
		return
	var hull := body.get_node("Tapered_1977_IndustrialPressureHull") as MeshInstance3D
	if hull.mesh == null or hull.mesh.get_surface_count() < 1 or \
			hull.mesh is BoxMesh:
		_fail("ship silhouette is still only a rectangular graybox primitive")
		return
	var station := stage.get_node_or_null("StationInterior_Prototype") as Node3D
	var cladding := station.get_node_or_null("OriginalStation_IndustrialCladding_NonOperational") as Node3D
	var terrain := station.get_node_or_null("DistantEarthworks_BackdropNotMineralInventory") as Node3D
	var dock := station.get_node_or_null("SpacefaringDock") as Node3D
	if cladding == null or cladding.get_child_count() < 45 or \
			terrain == null or terrain.get_child_count() < 20 or \
			dock.get_node_or_null("OriginalOwnedShip_IndustrialCladding_VisualOnly") == null:
		_fail("retro-industrial colony/berth or geological presentation missing")
		return
	if not cladding.find_children("*","CollisionObject3D",true,false).is_empty() or \
			not terrain.find_children("*","CollisionObject3D",true,false).is_empty():
		_fail("decorative colony created fake original-world collision")
		return
	if stage.material.albedo_texture == null:
		_fail("station render material remains flat/noise-free")
		return
	var stars := stage.get_node_or_null("NativeAsteroidExternalView/DistantStarfield_VISUAL_ONLY_NotOriginalEntities") as MultiMeshInstance3D
	if stars == null or stars.multimesh == null or \
			stars.multimesh.instance_count != VIS.HIGH_BACKGROUND_STARS or \
			stars.multimesh.visible_instance_count not in [VIS.HIGH_BACKGROUND_STARS,VIS.BALANCED_BACKGROUND_STARS]:
		_fail("draw-distance / single-call quality starfield missing")
		return
	stage._visual_high_quality = false
	stage._apply_visual_quality()
	if absf(stage.exterior_camera.far - VIS.BALANCED_DISTANCE_M)>1.0 or \
			stars.multimesh.visible_instance_count != VIS.BALANCED_BACKGROUND_STARS:
		_fail("balanced view distance not genuinely applied")
		return
	stage._visual_high_quality = true
	stage._apply_visual_quality()
	if absf(stage.exterior_camera.far - VIS.HIGH_DISTANCE_M)>1.0 or \
			stars.multimesh.visible_instance_count != VIS.HIGH_BACKGROUND_STARS:
		_fail("R9700 high preset did not raise actual camera draw distance")
		return
	# Verified navigation projection only; no physics world mutation.
	stage._verified_station_nav = {"found":true,"range_m":6500.0,
		"dx_mm":6500000,"dy_mm":0,"dz_mm":0}
	stage._refresh_station_visual_target()
	if not stage._visual_station_target.visible or \
			stage._visual_station_target.position.distance_to(Vector3(6500.0,0,0)) > 0.01:
		_fail("original C++ distant station not rendered at honest relative coordinate")
		return
	stage._visual_high_quality = false
	stage._apply_visual_quality()
	if stage._visual_station_target.visible:
		_fail("low quality preset draws station beyond the actual clipping range")
		return
	if stage.new_game_confirm == null:
		_fail("operator New Game confirmation dialog unavailable")
		return
	if stage.new_game_button == null or not stage.new_game_button.text.contains("backup"):
		_fail("visible safe New Game menu missing")
		return
	print("SG-VIS01 REAL GODOT PASS: 1977 tapered hull, noncolliding station retrofit, textured metal, high 8.5km vs balanced 1.8km camera, native station beacon culling, safe reset UI; NOT physical R9700 approval")
	quit(0)
