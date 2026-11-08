extends Node3D
## Self-contained monochrome FPS test arena: primitive meshes and true
## Godot physics collisions. NOT a replacement writer for native galaxy state.

const PLAYER_SCRIPT := preload("res://world/player_controller.gd")
const MONOCHROME := Color(0.76, 0.76, 0.76, 1.0)
var material: StandardMaterial3D
var player: CharacterBody3D
var status_label: Label
var fps_label: Label
var _fps_accumulator := 0.0

func _ready() -> void:
	material = StandardMaterial3D.new()
	material.albedo_color = MONOCHROME
	material.roughness = 1.0
	material.metallic = 0.0
	_setup_environment()
	_build_station()
	_build_objects()
	_build_player()
	_build_hud()

func _setup_environment() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.10, 0.10, 0.10)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.63, 0.63, 0.63)
	env.ambient_light_energy = 0.7
	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-52.0, -38.0, 0.0)
	sun.light_energy = 1.5
	sun.shadow_enabled = false
	add_child(sun)

func _solid_box(label: String, pos: Vector3, dimensions: Vector3) -> StaticBody3D:
	var body := StaticBody3D.new()
	body.name = label
	body.position = pos
	body.set_meta("inspect_label", label)
	body.collision_layer = 1
	var mesh := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = dimensions
	mesh.mesh = box
	mesh.material_override = material
	body.add_child(mesh)
	var collision := CollisionShape3D.new()
	var shape := BoxShape3D.new()
	shape.size = dimensions
	collision.shape = shape
	body.add_child(collision)
	add_child(body)
	return body

func _solid_cylinder(label: String, pos: Vector3, height: float, radius: float) -> StaticBody3D:
	var body := StaticBody3D.new()
	body.name = label
	body.position = pos
	body.set_meta("inspect_label", label)
	body.collision_layer = 1
	var mesh := MeshInstance3D.new()
	var cyl := CylinderMesh.new()
	cyl.top_radius = radius
	cyl.bottom_radius = radius
	cyl.height = height
	cyl.radial_segments = 7
	mesh.mesh = cyl
	mesh.material_override = material
	body.add_child(mesh)
	var collider := CollisionShape3D.new()
	var shape := CylinderShape3D.new()
	shape.radius = radius
	shape.height = height
	collider.shape = shape
	body.add_child(collider)
	add_child(body)
	return body

func _build_station() -> void:
	_solid_box("Station floor", Vector3(0, -0.25, 0), Vector3(64, 0.5, 64))
	# Four outer boundaries: no holes in the play test area.
	_solid_box("North perimeter", Vector3(0, 2.0, -32), Vector3(64, 4, 0.5))
	_solid_box("South perimeter", Vector3(0, 2.0, 32), Vector3(64, 4, 0.5))
	_solid_box("East perimeter", Vector3(32, 2.0, 0), Vector3(0.5, 4, 64))
	_solid_box("West perimeter", Vector3(-32, 2.0, 0), Vector3(0.5, 4, 64))
	# Wide-open doorway and a simple accessible station corridor.
	_solid_box("Hall west wall", Vector3(-5.0, 2.0, -7), Vector3(0.4, 4, 28))
	_solid_box("Hall east wall", Vector3(5.0, 2.0, -7), Vector3(0.4, 4, 28))
	_solid_box("Control room west", Vector3(-14, 2.0, -22), Vector3(18, 4, 0.4))
	_solid_box("Control room east", Vector3(14, 2.0, -22), Vector3(18, 4, 0.4))
	_solid_box("Landing strip", Vector3(17, 0.015, 8), Vector3(20, 0.03, 2))

func _build_objects() -> void:
	_solid_cylinder("NPC prototype — no arms", Vector3(0.0, 1.0, -5.0), 2, 0.38)
	_solid_cylinder("Life support module", Vector3(-2.5, 1.2, -11), 2.4, 0.8)
	_solid_box("Resource crate 01", Vector3(-2.3, 0.65, -2.5), Vector3(1.3, 1.3, 1.3))
	_solid_box("Resource crate 02", Vector3(3, 0.45, 0), Vector3(0.9, 0.9, 0.9))
	_solid_cylinder("Asteroid sample", Vector3(16, 1.3, 2), 2.6, 1.35)
	# A silhouette made from boxes to prove scale and collision, not an asset pipeline.
	_solid_box("Ship hull (placeholder)", Vector3(17.0, 1.25, -11.0), Vector3(8, 2.5, 3.2))
	_solid_box("Ship wing L", Vector3(17.0, 0.65, -14.0), Vector3(4.2, 0.4, 4.0))
	_solid_box("Ship wing R", Vector3(17.0, 0.65, -8.0), Vector3(4.2, 0.4, 4.0))
	_solid_box("Docking control console", Vector3(7.6, 0.8, -12), Vector3(1.3, 1.6, 1.0))

func _build_player() -> void:
	player = CharacterBody3D.new()
	player.name = "LocalGrayboxPerson_NOT_NATIVE_PLAYER"
	player.position = Vector3(0, 1.45, 12.0)
	player.set_script(PLAYER_SCRIPT)
	add_child(player)
	player.inspected.connect(_on_inspected)

func _build_hud() -> void:
	var hud := CanvasLayer.new()
	add_child(hud)
	var panel := PanelContainer.new()
	panel.position = Vector2(15, 15)
	panel.custom_minimum_size = Vector2(680, 72)
	hud.add_child(panel)
	var contents := VBoxContainer.new()
	panel.add_child(contents)
	var title := Label.new()
	title.text = "SINGLE-GALAXY | STANDALONE GRAYBOX FPS 0.0.1"
	title.add_theme_font_size_override("font_size", 18)
	contents.add_child(title)
	var subtitle := Label.new()
	subtitle.text = "LOCAL MOVEMENT TEST ONLY — NOT CONNECTED TO NATIVE GALAXY"
	contents.add_child(subtitle)
	status_label = Label.new()
	status_label.text = "WASD walk   Mouse look   Shift sprint   Space jump   E inspect   Esc release mouse"
	contents.add_child(status_label)
	fps_label = Label.new()
	fps_label.position = Vector2(16, 105)
	fps_label.add_theme_font_size_override("font_size", 15)
	hud.add_child(fps_label)
	var reticle := Label.new()
	reticle.text = "+"
	reticle.add_theme_font_size_override("font_size", 25)
	reticle.set_anchors_and_offsets_preset(Control.PRESET_CENTER)
	hud.add_child(reticle)

func _on_inspected(display_name: String) -> void:
	status_label.text = "Inspected: " + display_name + " (prototype only; no native action)"

func _process(delta: float) -> void:
	_fps_accumulator += delta
	if _fps_accumulator >= 0.5 and is_instance_valid(fps_label):
		fps_label.text = "FPS: %d | Godot physics 60 Hz | primitive monochrome scene" % Engine.get_frames_per_second()
		_fps_accumulator = 0.0
