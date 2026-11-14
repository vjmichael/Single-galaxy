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
var _visible_public := 0
var _visible_staff := 0
var _moving_public := 0
var _walk_seconds := 0.0
const MAX_ANIMATED_TRAVELERS := 8
const MAX_NATIVE_WALKERS := 4
var _verified_walkers: MultiMeshInstance3D
var _has_native_corridor_frame := false

func _ready() -> void:
	name = "DockAndColonyPopulationLOD_ReadOnly"
	_public_mesh = _new_batch("ColonyCivilianProxies_VisualOnly", Color(0.54,0.61,0.65))
	_dock_mesh = _new_batch("AssignedDockWorkerProxies_VisualOnly", Color(0.91,0.68,0.20))
	_verified_walkers = _new_batch("VerifiedOriginalNPCPositions_ReadOnly",
		Color(0.21,0.79,0.84))
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
	many.instance_count = MAX_PUBLIC_VISUALS if label.begins_with("Colony") else \
		MAX_NATIVE_WALKERS if label.begins_with("Verified") else MAX_DOCK_WORKER_VISUALS
	many.use_colors = label.begins_with("Verified")
	many.visible_instance_count = 0
	node.multimesh = many
	add_child(node)
	return node

func clear() -> void:
	_last_station = 0
	_visible_public = 0
	_visible_staff = 0
	_moving_public = 0
	_walk_seconds = 0.0
	_has_native_corridor_frame = false
	if _verified_walkers != null and _verified_walkers.multimesh != null:
		_verified_walkers.multimesh.visible_instance_count = 0
	if _public_mesh != null and _public_mesh.multimesh != null:
		_public_mesh.multimesh.visible_instance_count = 0
	if _dock_mesh != null and _dock_mesh.multimesh != null:
		_dock_mesh.multimesh.visible_instance_count = 0

func show_source_traffic(frame: Dictionary, station: int) -> void:
	# A passenger is never drawn from cohort pressure or a guessed name.
	# Only original, bounded waiting NPC count animates existing proxies.
	if station<=0 or station!=_last_station or _public_mesh==null or \
			int(frame.get("boarding_waiting",-1))<0 or \
			int(frame.get("boarding_waiting",-1))>32 or \
			int(frame.get("gangways",-1))<0 or \
			int(frame.get("gangways",-1))>2:
		_moving_public=0
		return
	var gate_limit := 0 if int(frame.get("gangways",0))==0 else MAX_ANIMATED_TRAVELERS
	_moving_public = 0 if _has_native_corridor_frame else mini(_visible_public,
		mini(int(frame["boarding_waiting"]),gate_limit))

func show_native_walkers(frame: Dictionary,station: int) -> void:
	if _verified_walkers==null or _verified_walkers.multimesh==null:
		return
	var shown: Array = frame.get("walkers",[])
	if station<=0 or int(frame.get("station",-1))!=station or \
			shown.size()>MAX_NATIVE_WALKERS:
		_verified_walkers.multimesh.visible_instance_count=0
		_has_native_corridor_frame=false
		return
	if station!=_last_station:
		clear() # station change cannot keep prior colony's people visible
		_last_station=station
	var seen: Dictionary={}
	for i in range(shown.size()):
		var person: Dictionary=shown[i]
		var original_id:=int(person.get("npc",0))
		var kind:=int(person.get("lane",0))
		var x:=int(person.get("x_mm",90000))
		var z:=int(person.get("z_mm",90000))
		if original_id<=0 or kind<1 or kind>3 or abs(x)>8000 or \
				abs(z)>8000 or seen.has(original_id):
			_verified_walkers.multimesh.visible_instance_count=0
			_has_native_corridor_frame=false
			return
		seen[original_id]=true
		_verified_walkers.multimesh.set_instance_transform(i,
			Transform3D(Basis.IDENTITY,
				Vector3(float(x)*0.001,0.83,float(z)*0.001)))
		var shade:=Color(0.94,0.71,0.22) if kind==1 else \
			Color(0.22,0.79,0.87) if kind==2 else Color(0.52,0.86,0.47)
		_verified_walkers.multimesh.set_instance_color(i,shade)
	_verified_walkers.multimesh.visible_instance_count=shown.size()
	_has_native_corridor_frame=true
	# Once the original named NPC coordinates arrive, suppress the old
	# synthetic pedestrian loop instead of displaying two copies.
	_moving_public=0

func _process(delta: float) -> void:
	if _moving_public<=0 or _last_station<=0 or _public_mesh==null or \
			_public_mesh.multimesh==null:
		return
	_walk_seconds = fposmod(_walk_seconds+minf(delta,0.25),30.0)
	# Eight transforms maximum; purely visual station-lane interpolation.
	# Movement never writes SQLite, NPC position, boarding, collision or AI.
	for i in range(_moving_public):
		var progress := fposmod(_walk_seconds*0.11+float(i)*0.15,1.0)
		var start := Vector3(-2.3+float(i%4)*0.55,0.83,3.3)
		var ending := Vector3(-0.75+float(i%4)*0.48,0.83,0.55)
		var pos := start.lerp(ending,progress)
		_public_mesh.multimesh.set_instance_transform(i,
			Transform3D(Basis.IDENTITY,pos))

func show_source_counts(frame: Dictionary, station: int) -> void:
	# Zero/stale source never creates an artificial crowd.
	if (station <= 0 or station != int(frame.get("station", -1)) or
			int(frame.get("source_count",-1)) < 0 or
			int(frame.get("dock_staff",-1)) < 0):
		clear()
		return
	_last_station = station
	var public_count := mini(int(frame["source_count"]), MAX_PUBLIC_VISUALS)
	var staff_count := mini(int(frame["dock_staff"]), MAX_DOCK_WORKER_VISUALS)
	_visible_public = public_count
	_visible_staff = staff_count
	_moving_public = 0 if _has_native_corridor_frame else mini(_moving_public,public_count)
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
