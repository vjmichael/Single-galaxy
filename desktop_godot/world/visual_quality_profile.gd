extends RefCounted
## VIS-01 quality policy. User-controlled GPU presentation only: no change to
## native asteroid sensor, original ship coordinates, or collision range.
## Inertial stars are background illumination cues, NEVER resource targets.

const HIGH_DISTANCE_M := 8500.0
const BALANCED_DISTANCE_M := 1800.0
const HIGH_BACKGROUND_STARS := 1550
const BALANCED_BACKGROUND_STARS := 310

static func is_r9700_adapter() -> bool:
	return RenderingServer.get_video_adapter_name().to_upper().contains("R9700")

static func distance_m(high: bool) -> float:
	return HIGH_DISTANCE_M if high else BALANCED_DISTANCE_M

static func apply_camera(camera: Camera3D, high: bool) -> void:
	camera.near = 0.22
	camera.far = distance_m(high)

static func add_inertial_background(parent: Node3D) -> MultiMeshInstance3D:
	var node := MultiMeshInstance3D.new()
	node.name = "DistantStarfield_VISUAL_ONLY_NotOriginalEntities"
	var dots := SphereMesh.new()
	dots.radius = 0.78
	dots.height = 1.56
	dots.radial_segments = 6
	dots.rings = 3
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.albedo_color = Color(0.73,0.79,0.85,1.0)
	mat.emission_enabled = true
	mat.emission = Color(0.42,0.60,0.77,1.0)
	mat.emission_energy_multiplier = 0.85
	node.material_override = mat
	var multi := MultiMesh.new()
	multi.transform_format = MultiMesh.TRANSFORM_3D
	multi.mesh = dots
	multi.instance_count = HIGH_BACKGROUND_STARS
	var rng := RandomNumberGenerator.new()
	rng.seed = 19771977
	for index in range(HIGH_BACKGROUND_STARS):
		var phi := rng.randf_range(0.0,TAU)
		var cos_theta := rng.randf_range(-0.92,0.92)
		var y := cos_theta
		var radial := sqrt(1.0-y*y)
		var r := rng.randf_range(650.0,7600.0)
		var center := Vector3(cos(phi)*radial*r,y*r,sin(phi)*radial*r)
		var scale_val := rng.randf_range(0.54,1.55)
		multi.set_instance_transform(index,
			Transform3D(Basis.IDENTITY.scaled(Vector3.ONE*scale_val),center))
	node.multimesh = multi
	parent.add_child(node)
	return node

static func set_background_quality(stars: MultiMeshInstance3D, high: bool) -> void:
	if stars == null or stars.multimesh == null:
		return
	stars.multimesh.visible_instance_count = HIGH_BACKGROUND_STARS if high else BALANCED_BACKGROUND_STARS
