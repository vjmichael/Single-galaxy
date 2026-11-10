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

static func _material(color: Color, unshaded: bool = false) -> StandardMaterial3D:
	var mat := StandardMaterial3D.new()
	mat.albedo_color = color
	mat.roughness = 0.91
	if unshaded:
		mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
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
	sign.pixel_size = 0.006
	sign.font_size = font
	sign.modulate = color
	sign.no_depth_test = true
	parent.add_child(sign)
	return sign

static func build(station: Node3D) -> void:
	# Colony represents home, civilian industry and the safe walking origin.
	# It is visibly distinct from the docked vessel and the departure berth.
	var colony := Node3D.new()
	colony.name = "ColonyArrival"
	station.add_child(colony)
	_box(colony, "Colony plaza paving", Vector3(1.6, 0.046, 3.05),
		Vector3(8.3, 0.06, 6.0), COLONY_DECK)
	_box(colony, "Original native person colony spawn emblem",
		Vector3(COLONY_SPAWN.x, 0.086, COLONY_SPAWN.z + 0.54),
		Vector3(0.85, 0.014, 0.55), ROUTE_ACCENT, true)
	_sign(colony, "Colony arrival wayfinding", "COLONY ARRIVALS\nHABITATS • SERVICES",
		Vector3(1.6, 3.2, 3.4), COLONY_LIGHT)
	# Colony buildings outside the +/-5m native corridor walls; they are
	# skyline / production context, never unregistered collidable blockers.
	for side in [-1, 1]:
		var x: float = float(side) * 7.1
		_box(colony, "Colony habitation block %d" % side,
			Vector3(x, 1.75, 3.6), Vector3(2.7, 3.5, 3.9),
			Color(0.41, 0.45, 0.49, 1.0))
		_box(colony, "Colony habitat roof %d" % side,
			Vector3(x, 3.55, 3.6), Vector3(3.1, 0.18, 4.25),
			Color(0.31, 0.35, 0.39, 1.0))
		for index in range(3):
			_box(colony, "Colony window %d-%d" % [side, index],
				Vector3(x - float(side)*1.37, 2.0, 2.6 + float(index)*0.94),
				Vector3(0.035, 0.6, 0.58), Color(0.22, 0.72, 0.88, 1.0), true)
	_box(colony, "Colony habitat utility tower",
		Vector3(-7.4, 2.5, -2.8), Vector3(2.4, 5.0, 2.6),
		Color(0.46, 0.50, 0.54, 1.0))
	_sign(colony, "Colony departure direction", "TO DOCK  /  SHIP DEPARTURES",
		Vector3(1.0, 2.45, 0.75), PATH_COLOR, 29)

	# A short pedestrian route links original PERSON genesis directly to the
	# exact old native (0,0) hatch so existing persisted saves still work.
	var corridor := Node3D.new()
	corridor.name = "ColonyToDockWalkway"
	station.add_child(corridor)
	for i in range(5):
		var x: float = 1.55 - float(i)*0.31
		_box(corridor, "Native hatch directional stripe %02d" % i,
			Vector3(x, 0.090, 0.0), Vector3(0.20, 0.014, 0.13),
			PATH_COLOR, true)
	_sign(corridor, "Pedestrian dock threshold", "DOCK CHECKPOINT\nBOARD AT THE CYAN RAMP",
		Vector3(-0.05, 3.15, -1.15), PATH_COLOR)

	# The dock is the transition from the *colony* to space flight. No
	# imaginary new planet/sector is created: the same original docked
	# ShipID is viewed here at its real owner-controlled station berth.
	var dock := Node3D.new()
	dock.name = "SpacefaringDock"
	station.add_child(dock)
	_box(dock, "Dock charcoal deck", Vector3(0.0, 0.042, -4.0),
		Vector3(9.0, 0.06, 7.8), DOCK_DECK)
	_box(dock, "Ship embarkation ramp", Vector3(0.0, 0.084, -1.6),
		Vector3(1.5, 0.014, 3.2), Color(0.48, 0.54, 0.58, 1.0))
	# Non-colliding berth markings guide the eye to the OWNED white ship.
	for side in [-1, 1]:
		var edge_x: float = float(side)*3.9
		_box(dock, "Dock hazard stripe %d" % side,
			Vector3(edge_x, 0.081, -4.45), Vector3(0.15, 0.012, 5.4),
			ROUTE_ACCENT, true)
		_box(dock, "Dock berth beacon %d" % side,
			Vector3(float(side)*3.05, 0.13, -1.15),
			Vector3(0.23, 0.25, 0.23), PATH_COLOR, true)
	_sign(dock, "Departure dock label", "DEPARTURES  /  TO SPACE",
		Vector3(0.0, 3.8, -4.6), COLONY_LIGHT)
	# White hull, ramp and active (0,0) checkpoint share the central axis.
	# This builder deliberately creates NO collision or boarding callback.
