extends CharacterBody3D
## S6-F local inspection avatar, NEVER an original-world player or NPC.
## Constructed ONLY after a native original shuttle descent observation.
## Camera movement and Godot collision are presentation, not saved travel.
const INSPECTION_SPEED_MPS := 3.5
const SURFACE_GRAVITY_MPS2 := 16.0
const LOOK_SENSITIVITY := 0.002
var _yaw := 0.0
var _pitch := 0.0
var camera: Camera3D

func _ready() -> void:
    name = "ReadOnlySurfaceInspector_NOT_PLAYER"
    collision_layer = 0
    collision_mask = 1
    var body := CollisionShape3D.new()
    var capsule := CapsuleShape3D.new()
    capsule.height = 1.6
    capsule.radius = 0.3
    body.shape = capsule
    add_child(body)
    camera = Camera3D.new()
    camera.position = Vector3(0.0, 0.58, 0.0)
    camera.fov = 77.0
    add_child(camera)
    camera.current = true
    Input.mouse_mode = Input.MOUSE_MODE_CAPTURED

func _unhandled_input(event: InputEvent) -> void:
    if event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_ESCAPE:
        Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
    elif event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
        Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
    elif event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED and get_window().has_focus():
        _yaw -= event.relative.x * LOOK_SENSITIVITY
        _pitch = clampf(_pitch - event.relative.y * LOOK_SENSITIVITY, -1.25, 1.25)

func _physics_process(_delta: float) -> void:
    # Spherical local visual frame follows the original display mesh.
    # This must NEVER be serialized or emitted as a flight / landing command.
    var up := global_position.normalized()
    if up.length_squared() < 0.9:
        velocity = Vector3.ZERO
        return
    up_direction = up
    var east := Vector3.UP.cross(up)
    if east.length_squared() < 0.0001:
        east = Vector3.FORWARD.cross(up)
    east = east.normalized()
    var north := up.cross(east).normalized()
    var forward := (north * cos(_yaw) + east * sin(_yaw)).normalized()
    var right := (east * cos(_yaw) - north * sin(_yaw)).normalized()
    look_at(global_position + forward, up)
    camera.rotation.x = _pitch
    var movement := Vector3.ZERO
    if get_window().has_focus() and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
        if Input.is_physical_key_pressed(KEY_W) or Input.is_key_pressed(KEY_UP):
            movement += forward
        if Input.is_physical_key_pressed(KEY_S) or Input.is_key_pressed(KEY_DOWN):
            movement -= forward
        if Input.is_physical_key_pressed(KEY_D) or Input.is_key_pressed(KEY_RIGHT):
            movement += right
        if Input.is_physical_key_pressed(KEY_A) or Input.is_key_pressed(KEY_LEFT):
            movement -= right
    # Collision is strictly local Godot trimesh; a terrain overlap cannot
    # authorize planetary construction, descent, cargo transfer or NPC moves.
    velocity = movement.normalized() * INSPECTION_SPEED_MPS - up * SURFACE_GRAVITY_MPS2
    move_and_slide()

func _notification(what: int) -> void:
    if what == NOTIFICATION_WM_WINDOW_FOCUS_OUT:
        Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
        velocity = Vector3.ZERO
