extends RefCounted
## VIS-01: procedural 1970s retro-industrial hard-SF silhouette for ONE
## original native-owned ship. Decoration only. All geometry is noncolliding
## and never changes EntityId, hull collision, fittings or original assets.
## X+ is the ship's forward vector; Godot local up is Y.

const CREAM := Color(0.79,0.80,0.76,1.0)
const PORCELAIN := Color(0.90,0.88,0.78,1.0)
const GRAPHITE := Color(0.15,0.18,0.21,1.0)
const STEEL := Color(0.37,0.41,0.42,1.0)
const GOLDOCHRE := Color(0.81,0.56,0.20,1.0)
const CERULEAN := Color(0.14,0.57,0.65,1.0)
const ENGINE := Color(0.28,0.83,0.99,1.0)

static func _mat(color: Color, metallic: float, rough: float, glow: bool = false) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = color
	m.metallic = metallic
	m.roughness = rough
	if glow:
		m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		m.emission_enabled = true
		m.emission = color
		m.emission_energy_multiplier = 1.8
	return m

static func _box(parent: Node3D, name: String, at: Vector3, size: Vector3,
		mat: Material) -> MeshInstance3D:
	var part := MeshInstance3D.new()
	part.name = name
	var mesh := BoxMesh.new()
	mesh.size = size
	part.mesh = mesh
	part.position = at
	part.material_override = mat
	parent.add_child(part)
	return part

static func _bar(parent: Node3D, name: String, at: Vector3,
		length: float, radius: float, mat: Material) -> MeshInstance3D:
	var part := MeshInstance3D.new()
	part.name = name
	var shape := CylinderMesh.new()
	shape.top_radius = radius
	shape.bottom_radius = radius
	shape.height = length
	shape.radial_segments = 14
	part.mesh = shape
	part.rotation.z = PI*0.5 # engine axes and tanks run along original X
	part.position = at
	part.material_override = mat
	parent.add_child(part)
	return part

static func _tapered_hull() -> ArrayMesh:
	# A genuine 3D eight-face tapered hull, not an elongated box primitive.
	# Surface normals are derived from actual triangulated industrial skin.
	var cross: Array[Vector3] = [
		Vector3(-3.95,0.90,1.25), Vector3(-3.95,0.90,-1.25),
		Vector3(-3.95,-0.90,-1.25),Vector3(-3.95,-0.90,1.25),
		Vector3(3.65,0.77,1.16),Vector3(3.65,0.77,-1.16),
		Vector3(3.65,-0.77,-1.16),Vector3(3.65,-0.77,1.16),
		Vector3(5.75,0.36,0.44),Vector3(5.75,0.36,-0.44),
		Vector3(5.75,-0.36,-0.44),Vector3(5.75,-0.36,0.44)]
	var surface := SurfaceTool.new()
	surface.begin(Mesh.PRIMITIVE_TRIANGLES)
	for section in range(2):
		for edge in range(4):
			var a := section*4 + edge
			var b := section*4 + (edge+1)%4
			var c := (section+1)*4 + edge
			var d := (section+1)*4 + (edge+1)%4
			for idx in [a,b,d,a,d,c]:
				surface.add_vertex(cross[idx])
	for idx in [0,3,2,0,2,1,8,9,10,8,10,11]:
		surface.add_vertex(cross[idx])
	surface.generate_normals()
	return surface.commit()

static func decorate_external_body(body: Node3D, wings: Array[Node3D]) -> void:
	var ceramic := _mat(CREAM,0.32,0.73)
	var white := _mat(PORCELAIN,0.27,0.70)
	var slate := _mat(GRAPHITE,0.65,0.52)
	var steel := _mat(STEEL,0.69,0.62)
	var ochre := _mat(GOLDOCHRE,0.43,0.61)
	var glass := _mat(Color(0.10,0.20,0.27),0.42,0.21)
	var blue := _mat(ENGINE,0.0,0.30,true)
	var tapered := MeshInstance3D.new()
	tapered.name = "Tapered_1977_IndustrialPressureHull"
	tapered.mesh = _tapered_hull()
	tapered.material_override = ceramic
	body.add_child(tapered)
	# Raised pressure-hull frames, docking-bay seams and the recessed canopy
	# provide readable fore/aft direction from a third-person camera.
	for i in range(7):
		var x := -3.2+float(i)*0.92
		_box(body,"Bolted axial frame %d" % i,Vector3(x,0.95,0),
			Vector3(0.09,0.12,2.6),steel)
		_box(body,"Service belly rib %d" % i,Vector3(x,-0.99,0),
			Vector3(0.10,0.13,2.32),slate)
	_box(body,"Recessed dark cockpit windshield",Vector3(3.17,1.20,0),
		Vector3(1.75,0.54,1.75),glass)
	_box(body,"Anti-glare canopy cap",Vector3(2.65,1.53,0),
		Vector3(1.3,0.16,1.7),slate)
	_box(body,"Ochre forward warning stripe",Vector3(4.36,0.51,0),
		Vector3(0.19,0.10,1.38),ochre)
	_box(body,"Forward sensor chin",Vector3(5.45,-0.48,0),
		Vector3(1.18,0.27,0.43),slate)
	for sign in [-1,1]:
		var z := float(sign)*1.55
		_bar(body,"External fuel/coolant drum %d" % sign,
			Vector3(-0.75,-0.12,z),5.85,0.48,steel)
		_bar(body,"Drum pressure-stripe %d" % sign,
			Vector3(-1.15,-0.12,z),0.16,0.51,ochre)
		_bar(body,"Engine protective cowling %d" % sign,
			Vector3(-4.0,-0.22,z),1.40,0.71,slate)
		_bar(body,"Drive exhaust core %d" % sign,
			Vector3(-4.76,-0.22,z),0.08,0.45,blue)
		for fin in range(4):
			_box(body,"Radiator slit %d-%d" % [sign,fin],
				Vector3(-2.5+float(fin)*0.42,1.07,float(sign)*0.91),
				Vector3(0.17,0.22,0.07),slate)
	# Wings already exist as the underlying 6m physical visual proxies.
	# Attach real fins / navigation strobes without changing the parent's
	# four stable child identities (hull, two wings, camera).
	for index in range(wings.size()):
		var wing := wings[index]
		var side := -1 if index == 0 else 1
		_box(wing,"Wing armored cover",Vector3(-0.58,0.18,float(side)*0.30),
			Vector3(2.65,0.12,1.5),ceramic)
		_box(wing,"Wing leading-edge steel spar",Vector3(0.84,0.07,float(side)*0.25),
			Vector3(0.24,0.18,1.65),steel)
		_box(wing,"Wing tip ochre hazard",Vector3(-0.28,0.15,float(side)*0.65),
			Vector3(0.61,0.14,0.20),ochre)
		var light := _mat(Color(0.80,0.18,0.12) if side < 0 else
			Color(0.17,0.76,0.62),0.0,0.18,true)
		_box(wing,"Physical ship navigation light",Vector3(-0.45,0.16,float(side)*1.0),
			Vector3(0.21,0.16,0.22),light)
	# Original ship is a practical industrial vessel, NOT a sleek yacht.

static func build_docked(parent: Node3D) -> void:
	# Decorative reduced scale of the SAME ship identity at its already
	# existing (0,0) native station berth. Boarding still uses the old native
	# person/hull and no second ship is created.
	var root := Node3D.new()
	root.name = "OriginalOwnedShip_IndustrialCladding_VisualOnly"
	root.position = Vector3(0,1.38,-4.95)
	root.rotation.y = -PI*0.5
	root.scale = Vector3.ONE*0.58
	parent.add_child(root)
	var core := MeshInstance3D.new()
	core.name = "Docked industrial tapered hull"
	core.mesh = BoxMesh.new()
	core.material_override = _mat(CREAM,0.30,0.69)
	root.add_child(core)
	var wings: Array[Node3D] = []
	for side in [-1,1]:
		var wing := Node3D.new()
		wing.name = "Docked swept wing %d" % side
		wing.position = Vector3(-0.25,-0.3,float(side)*2.35)
		root.add_child(wing)
		wings.append(wing)
	decorate_external_body(core,wings)
