extends RefCounted
## FP3D-COLONY-01 — colony -> dock -> space-faring visual composition.
##
## The original C++ station-person PERSON is authoritative:
##   fresh station x_mm=1600, z_mm=0 => colony arrival plaza
##   boarding threshold x_mm=z_mm=0, each axis +/-600 => dock checkpoint
## These are VISUAL-ONLY geometries. No physics, owner rights, or SQLite
## state is introduced by this scene builder. Original native walkway
## barriers/refinery/NPC remain defined in NativeStationWalkGeometry.hpp.

const COLONY_SPAWN := Vector3(1.6, 0.0, 0.0)
const DOCK_GATE := Vector3(0.0, 0.0, 0.0)
const SHIP_BERTH := Vector3(0.0, 0.0, -4.9)

const COLONY_DECK := Color(0.50, 0.54, 0.58, 1.0)
const DOCK_DECK := Color(0.30, 0.34, 0.38, 1.0)
const PATH_COLOR := Color(0.17, 0.72, 0.83, 1.0)
const ROUTE_ACCENT := Color(0.91, 0.72, 0.24, 1.0)
const COLONY_LIGHT := Color(0.73, 0.82, 0.89, 1.0)
const INDUSTRIAL_IVORY := Color(0.75, 0.74, 0.68, 1.0)
const FRAME_STEEL := Color(0.23, 0.27, 0.29, 1.0)
const WEATHERED_BRASS := Color(0.65,0.45,0.23,1.0)
static var _patina_tex: ImageTexture

static func _get_patina() -> ImageTexture:
	if _patina_tex != null:
		return _patina_tex
	# Fine physically varied metal/paint surface, generated ONCE on startup.
	# Procedural 3D material, not a flat decal or internet image.
	var image := Image.create_empty(96,96,false,Image.FORMAT_RGBA8)
	var noise := FastNoiseLite.new()
	noise.seed = 19771977
	noise.frequency = 0.085
	for y in range(96):
		for x in range(96):
			var n := noise.get_noise_2d(float(x),float(y))
			var c := clampf(0.82 + n*0.11,0.69,0.98)
			image.set_pixel(x,y,Color(c,c,c,1.0))
	_patina_tex = ImageTexture.create_from_image(image)
	return _patina_tex

static func apply_station_material(mat: StandardMaterial3D) -> void:
	mat.albedo_texture = _get_patina()
	mat.metallic = 0.24
	mat.roughness = 0.83

static func _material(color: Color, unshaded: bool = false) -> StandardMaterial3D:
	var mat := StandardMaterial3D.new()
	mat.albedo_color = color
	mat.roughness = 0.84
	mat.metallic = 0.27
	if unshaded:
		mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	else:
		mat.albedo_texture = _get_patina()
	return mat

static func _box(parent: Node3D, name: String, at: Vector3,
		size: Vector3, color: Color, unshaded: bool = false) -> MeshInstance3D:
	var node := MeshInstance3D.new()
	node.name = name
	var mesh := BoxMesh.new()
	mesh.size = size
	node.mesh = mesh
	node.material_override = _material(color, unshaded)
	node.position = at
	# No CollisionShape3D. Native C++ remains the ONLY walk/board authority.
	parent.add_child(node)
	return node

static func _sign(parent: Node3D, name: String, wording: String,
		at: Vector3, color: Color, font: int = 35) -> Label3D:
	var sign := Label3D.new()
	sign.name = name
	sign.text = wording
	sign.position = at
	sign.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	sign.pixel_size = 0.0038
	sign.font_size = mini(font, 32)
	sign.modulate = color
	# Previous no-depth signage overlapped across the whole screen, even
	# through buildings. Physical occlusion keeps wayfinding legible.
	sign.no_depth_test = false
	parent.add_child(sign)
	return sign

static func _pipe(parent: Node3D, name: String, at: Vector3,
		length: float, radius: float, color: Color, vertical: bool = false) -> void:
	var part := MeshInstance3D.new()
	part.name = name
	var mesh := CylinderMesh.new()
	mesh.top_radius = radius
	mesh.bottom_radius = radius
	mesh.height = length
	mesh.radial_segments = 10
	part.mesh = mesh
	part.position = at
	part.rotation.z = 0.0 if vertical else PI*0.5
	part.material_override = _material(color)
	parent.add_child(part)

static func _station_shell_details(parent: Node3D) -> void:
	# Deliberately restrained structural shell, not the former crowded
	# plaza-side facade. The separate dock/worker district remains readable.
	var shell := Node3D.new()
	shell.name = "OriginalStation_IndustrialCladding_NonOperational"
	parent.add_child(shell)
	for side in [-1,1]:
		for i in range(13):
			var z := -10.0+float(i)*0.91
			_box(shell,"Pressure-shell segmented rib %d-%02d" % [side,i],
				Vector3(float(side)*10.7,2.4,z),Vector3(0.13,4.8,0.16),FRAME_STEEL)
			_box(shell,"Pressure-shell tempered panel %d-%02d" % [side,i],
				Vector3(float(side)*10.65,2.55,z+0.36),Vector3(0.09,3.9,0.60),INDUSTRIAL_IVORY)
	# A distant nonphysical horizon, never a simulated resource deposit.
	var vista := Node3D.new()
	vista.name = "DistantEarthworks_BackdropNotMineralInventory"
	parent.add_child(vista)
	for i in range(28):
		var x := (float(i)-13.5)*20.0
		var height := 7.0+absf(sin(float(i)*1.91))*13.0
		_box(vista,"Distant ridge %02d" % i,
			Vector3(x,height*0.5,-210.0),Vector3(21.0,height,14.0),
			Color(0.29,0.29,0.30))

static func _native_pedestrian_wayfinding(station: Node3D) -> void:
	# DOCK-06 visual representation of the exact original native station
	# corridors (millimeters are defined by Dock06Corridor.hpp). These
	# meshes and signs are NOT a new navmesh, physical collision or free NPC.
	var district := Node3D.new()
	district.name = "OriginalPedestrianDistrict_WayfindingOnly"
	station.add_child(district)
	var passengers := Node3D.new()
	passengers.name = "PassengerSecurityAndGangway"
	district.add_child(passengers)
	# Public corridor: source (1.7,3.0) to the berth at (0.9,-3.4).
	# Individual named NPC paths stay within the +/-0.54m native lanes.
	for i in range(9):
		var z := 2.8-float(i)*0.75
		_box(passengers,"Passenger floor lane %02d" % i,
			Vector3(0.9,0.15,z),Vector3(1.7,0.012,0.13),
			PATH_COLOR,true)
	for x in [0.34,1.45]:
		_box(passengers,"Security rail %.2f" % x,
			Vector3(x,0.53,-0.25),Vector3(0.05,1.04,1.65),FRAME_STEEL)
	_box(passengers,"Gangway check-in stop",
		Vector3(0.9,0.155,-3.40),Vector3(1.72,0.016,0.12),
		ROUTE_ACCENT,true)
	_sign(passengers,"Passenger gate direction",
		"BOARDING  /  CHECK IN  /  GANGWAY",
		Vector3(1.05,2.0,0.88),PATH_COLOR,21)
	_sign(passengers,"Arrival return direction",
		"ARRIVALS  /  COLONY COMMONS",
		Vector3(1.38,2.0,2.55),Color(0.52,0.86,0.47),20)
	var service := Node3D.new()
	service.name = "StaffRestrictedWalkway"
	district.add_child(service)
	# Native worker lane is x=-3.62m. It skirts the physical crate
	# collision and remains separate from the native passenger gate.
	for i in range(8):
		_box(service,"Worker lane segment %02d" % i,
			Vector3(-3.62,0.155,1.85-float(i)*0.77),
			Vector3(0.16,0.014,0.50),ROUTE_ACCENT,true)
	_box(service,"Worker dispatch checkpoint",
		Vector3(-3.55,0.155,-3.7),Vector3(0.9,0.014,0.13),
		ROUTE_ACCENT,true)
	_sign(service,"Staff movement identity",
		"DOCK CREW  /  AUTHORIZED SERVICE ROUTE",
		Vector3(-3.55,2.15,1.72),COLONY_LIGHT,19)
	# Fixed display lights help depth/wayfinding but do not provide
	# electric power, a navigation light, or physically constructed industry.
	for i in range(3):
		var overhead := OmniLight3D.new()
		overhead.name = "Wayfinding ceiling light %02d" % i
		overhead.position = Vector3(0.15,3.75,2.0-float(i)*2.6)
		overhead.light_color = Color(0.83,0.91,1.0)
		overhead.light_energy = 0.42
		overhead.omni_range = 5.5
		district.add_child(overhead)

static func build(station: Node3D) -> void:
	# The dock is a logistics district WITHIN the original station, not
	# a second gameplay colony. 3D layout is swappable by the creator;
	# source positions stay authoritative for every existing save.
	var colony := Node3D.new()
	colony.name = "ColonyArrival"
	station.add_child(colony)
	_box(colony,"Civilian arrival concourse",Vector3(2.1,0.048,3.5),
		Vector3(9.8,0.07,7.1),COLONY_DECK)
	_box(colony,"Original native person spawn",
		Vector3(COLONY_SPAWN.x,0.086,COLONY_SPAWN.z+0.55),
		Vector3(0.8,0.02,0.45),ROUTE_ACCENT,true)
	_sign(colony,"Civilian wayfinding","COLONY COMMONS  |  SERVICES",
		Vector3(2.5,2.4,4.1),COLONY_LIGHT,26)
	# Actual workshops/habitats require native construction. These panels
	# merely bound the public approach, not invented inhabited structures.
	for side in [-1,1]:
		var x := float(side)*6.6
		for i in range(5):
			_box(colony,"Concourse rail upright %d-%d" % [side,i],
				Vector3(x,0.85,0.7+float(i)*1.24),
				Vector3(0.10,1.62,0.10),FRAME_STEEL)
		_box(colony,"Concourse rail handhold %d" % side,
			Vector3(x,1.56,3.2),Vector3(0.12,0.09,6.3),FRAME_STEEL)

	var corridor := Node3D.new()
	corridor.name = "ColonyToDockWalkway"
	station.add_child(corridor)
	_box(corridor,"Public passenger bridge",Vector3(0.80,0.082,-0.44),
		Vector3(2.35,0.045,2.4),COLONY_DECK)
	for i in range(5):
		_box(corridor,"Passenger flow marker %02d" % i,
			Vector3(1.50-float(i)*0.30,0.113,-0.21),
			Vector3(0.13,0.014,0.13),PATH_COLOR,true)
	_sign(corridor,"Dock identity","PASSENGER AIRLOCK / DEPARTURES",
		Vector3(0.0,2.25,-1.45),PATH_COLOR,21)
	# One native airlock at (0,0). No second spawn, teleport, NPC or
	# boarding rule is created by the scene.
	_box(corridor,"Airlock threshold",Vector3(0.0,0.14,-0.78),
		Vector3(2.0,0.12,0.20),WEATHERED_BRASS)

	var dock := Node3D.new()
	dock.name = "SpacefaringDock"
	station.add_child(dock)
	# Multi-lane dock: center passenger flow, exterior independently
	# managed cargo areas, and outboard worker dispatch. The 3D meshes
	# have no collision; the C++ native world remains the single authority.
	_box(dock,"Space berth pressure deck",Vector3(0,0.045,-5.2),
		Vector3(9.1,0.065,9.1),DOCK_DECK)
	_box(dock,"Passenger centerline",Vector3(0,0.094,-2.5),
		Vector3(1.5,0.016,4.6),COLONY_DECK)
	_box(dock,"Native hatch embarkation ramp",Vector3(0,0.115,-1.85),
		Vector3(1.45,0.016,2.8),INDUSTRIAL_IVORY)
	for side in [-1,1]:
		var direction := float(side)
		_box(dock,"Independent freight vehicle lane %d" % side,
			Vector3(direction*6.6,0.05,-5.05),
			Vector3(2.7,0.07,9.2),FRAME_STEEL)
		_box(dock,"Freight-to-ship staging pad %d" % side,
			Vector3(direction*7.0,0.09,-7.35),
			Vector3(3.0,0.06,2.4),DOCK_DECK)
		for n in range(6):
			_box(dock,"Freight floor segmentation %d-%02d" % [side,n],
				Vector3(direction*6.5,0.101,-8.4+float(n)*1.25),
				Vector3(2.1,0.012,0.08),ROUTE_ACCENT,true)
		_box(dock,"Berth safety railing %d" % side,
			Vector3(direction*4.6,0.75,-5.6),Vector3(0.12,1.5,6.3),FRAME_STEEL)
		_sign(dock,"Freight lane signage %d" % side,
			"FREIGHT  /  NOT PUBLIC",
			Vector3(direction*7.0,2.1,-1.5),ROUTE_ACCENT,21)
	_sign(dock,"Passenger dock signage","PASSENGER BERTH  |  SHIP / SPACE",
		Vector3(0,3.0,-7.5),COLONY_LIGHT,25)
	# Distinct staff area is visually reserved for real named dock personnel.
	# No free workers are created by modeling a dispatch station.
	var staff := Node3D.new()
	staff.name = "DockOperations_StaffAllocationRequired"
	dock.add_child(staff)
	_box(staff,"Dispatch floor",Vector3(-7.3,0.08,0.15),
		Vector3(4.3,0.13,3.5),DOCK_DECK)
	_box(staff,"Worker check-in console",Vector3(-7.3,0.77,0.45),
		Vector3(2.8,1.3,0.62),INDUSTRIAL_IVORY)
	_box(staff,"Staff dispatch canopy",Vector3(-7.3,2.7,0.0),
		Vector3(4.5,0.13,3.4),FRAME_STEEL)
	_sign(staff,"Dock worker station","DOCK OPERATIONS / STAFF DISPATCH",
		Vector3(-7.3,2.05,0.5),COLONY_LIGHT,20)
	# No planted refinery or manufacturing machinery. Industry is remote
	# UI only after original physical facilities have been constructed.
	var cargo := Node3D.new()
	cargo.name = "DockCargoStaging_NoSeparateInventory"
	dock.add_child(cargo)
	_box(cargo,"Owner-specific staging platform",Vector3(7.3,0.11,0.55),
		Vector3(4.1,0.13,3.0),COLONY_DECK)
	_sign(cargo,"Owner hangar signage","LOCAL COLONY CARGO / I INVENTORY",
		Vector3(7.3,1.8,0.75),COLONY_LIGHT,20)
	# Keep the original berthing and ship-view nodes stable.
	for side in [-1,1]:
		_box(dock,"Physical docking cue %d" % side,
			Vector3(float(side)*3.0,0.14,-1.2),
			Vector3(0.19,0.22,0.19),PATH_COLOR,true)
	_native_pedestrian_wayfinding(station)
	_station_shell_details(station)
