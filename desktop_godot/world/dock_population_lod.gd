extends Node3D
## Dock/colony graphics LOD. One real NPC census observation may stand for
## a much larger simulated population; drawing NEVER spawns NPC identities,
## jobs, inventories or independent physics/collision bodies.
##
## Hard budgets remain constant even for millions of original residents.
const MAX_PUBLIC_VISUALS := 24
const MAX_DOCK_WORKER_VISUALS := 8
var _public_mesh: MultiMeshInstance3D
var _dock_mesh: MultiMeshInstance3D
var _last_station := 0

func _ready() -> void:
	name = "DockAndColonyPopulationLOD_ReadOnly"
	_public_mesh = _new_batch("ColonyCivilianProxies_VisualOnly", Color(0.54,0.61,0.65))
	_dock_mesh = _new_batch("AssignedDockWorkerProxies_VisualOnly", Color(0.91,0.68,0.20))
	clear()

func _new_batch(label: String, base_color: Color) -> MultiMeshInstance3D:
	var node := MultiMeshInstance3D.new()
	node.name = label
	var model := CapsuleMesh.new()
	model.radius = 0.21
	model.height = 1.60
	model.radial_segments = 8
	model.rings = 3
	var mat := StandardMaterial3D.new()
	mat.albedo_color = base_color
	mat.roughness = 0.85
	node.material_override = mat
	var many := MultiMesh.new()
	many.transform_format = MultiMesh.TRANSFORM_3D
	many.mesh = model
	many.instance_count = MAX_PUBLIC_VISUALS if label.begins_with("Colony") else MAX_DOCK_WORKER_VISUALS
	many.visible_instance_count = 0
	node.multimesh = many
	add_child(node)
	return node

func clear() -> void:
	_last_station = 0
	if _public_mesh != null and _public_mesh.multimesh != null:
		_public_mesh.multimesh.visible_instance_count = 0
	if _dock_mesh != null and _dock_mesh.multimesh != null:
		_dock_mesh.multimesh.visible_instance_count = 0

func show_source_counts(frame: Dictionary, station: int) -> void:
	# Zero/stale source never creates an artificial crowd.
	if station <= 0 or station != int(frame.get("station", -1)) or
			int(frame.get("source_count",-1)) < 0 or
			int(frame.get("dock_staff",-1)) < 0:
		clear()
		return
	_last_station = station
	var public_count := mini(int(frame["source_count"]), MAX_PUBLIC_VISUALS)
	var staff_count := mini(int(frame["dock_staff"]), MAX_DOCK_WORKER_VISUALS)
	_public_mesh.multimesh.visible_instance_count = public_count
	_dock_mesh.multimesh.visible_instance_count = staff_count
	# Crowd proxies occupy ONLY presentation lanes. Avoid loops over a
	# million original NPC identities and avoid Godot AI/physics per proxy.
	for i in range(public_count):
		var row := i/8
		var column := i%8
		var pos := Vector3(-2.4+float(column)*0.65,0.83,3.0+float(row)*0.86)
		_public_mesh.multimesh.set_instance_transform(i,Transform3D(Basis.IDENTITY,pos))
	for i in range(staff_count):
		var row := i/4
		var column := i%4
		var pos := Vector3(-8.35+float(column)*0.62,0.83,-0.55+float(row)*0.90)
		_dock_mesh.multimesh.set_instance_transform(i,Transform3D(Basis.IDENTITY,pos))
