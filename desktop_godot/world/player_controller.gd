extends CharacterBody3D
## FP3D-02 prototype: local Godot walking ONLY. Not authoritative galaxy custody.
## No browser, no ship thrust, no persistence, no weapons or fake remote control.

signal inspected(display_name: String)
signal native_motion_intent(heading: Vector3)
signal native_seat_intent()
signal native_ship_intents(actions: PackedStringArray)
var native_seated := false
var authoritative_mode := false

func use_native_authority(enabled: bool) -> void:
	authoritative_mode = enabled
	if not enabled:
		native_seated = false
	velocity = Vector3.ZERO

func use_native_seat(seated: bool) -> void:
	native_seated = seated
	velocity = Vector3.ZERO

const WALK_SPEED := 5.0
const RUN_SPEED := 7.5
const JUMP_SPEED := 4.5
const MOUSE_SENSITIVITY := 0.0019
const EYE_HEIGHT := 0.58
const GRAVITY := 17.0
const INPUT_CONFIG := preload("res://world/input_contract.gd")

var camera: Camera3D
var sight: RayCast3D

func _ready() -> void:
	INPUT_CONFIG.install()
	var collision := CollisionShape3D.new()
	var capsule := CapsuleShape3D.new()
	capsule.radius = 0.34
	capsule.height = 1.75
	collision.shape = capsule
	add_child(collision)
	camera = Camera3D.new()
	camera.position = Vector3(0.0, EYE_HEIGHT, 0.0)
	camera.fov = 78.0
	camera.current = true
	add_child(camera)
	sight = RayCast3D.new()
	sight.target_position = Vector3(0.0, 0.0, -3.2)
	sight.collision_mask = 1
	sight.enabled = true
	camera.add_child(sight)
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_ESCAPE:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		get_viewport().set_input_as_handled()
	elif event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
		if Input.mouse_mode != Input.MOUSE_MODE_CAPTURED:
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
			get_viewport().set_input_as_handled()
	elif event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		rotate_y(-event.relative.x * MOUSE_SENSITIVITY)
		camera.rotation.x = clamp(camera.rotation.x - event.relative.y * MOUSE_SENSITIVITY, -1.45, 1.45)
		get_viewport().set_input_as_handled()

func _physics_process(delta: float) -> void:
	if authoritative_mode:
		# Native mode NEVER applies Godot movement or jumps. Only the trusted
		# C++ operation-journal transaction may advance the player position.
		velocity = Vector3.ZERO
		# E is a focused-window action, not a mouse-capture action. The old
		# implementation silently ignored boarding when the Windows cursor was
		# released with Esc or after clicking the standalone game controls.
		# Native C++ still authorizes the actual boarding/seat transition.
		if get_window().has_focus() and Input.is_action_just_pressed("fps_inspect"):
			native_seat_intent.emit()
		if Input.mouse_mode == Input.MOUSE_MODE_CAPTURED and get_window().has_focus():
			if native_seated:
				# Only intentional physical control requests. C++ retains ship motion,
				# yaw, thrust admission and persistent world authority.
				var pilot_actions := PackedStringArray()
				if Input.is_action_pressed("ship_undock"):
					pilot_actions.append("UND")
				if Input.is_action_just_pressed("ship_dock"):
					pilot_actions.append("DCK")
				if Input.is_action_pressed("ship_counterthrust"):
					pilot_actions.append("BRAKE")
				if Input.is_action_pressed("ship_yaw_left"):
					pilot_actions.append("LEFT")
				if Input.is_action_pressed("ship_yaw_right"):
					pilot_actions.append("RIGHT")
				if Input.is_action_pressed("ship_strafe_port"):
					pilot_actions.append("PORT")
				if Input.is_action_pressed("ship_strafe_starboard"):
					pilot_actions.append("STAR")
				if Input.is_action_pressed("ship_thrust_forward"):
					pilot_actions.append("FWD")
				if Input.is_action_pressed("ship_thrust_reverse"):
					pilot_actions.append("REV")
				if not pilot_actions.is_empty():
					native_ship_intents.emit(pilot_actions)
			else:
				var axis := Input.get_vector("fps_left", "fps_right", "fps_forward", "fps_backward")
				var direction := (global_transform.basis * Vector3(axis.x, 0.0, axis.y)).normalized()
				native_motion_intent.emit(direction)
		return
	# The arrow keys NEVER generate thrust while on foot. No input auto-repeat needed.
	if not is_on_floor():
		velocity.y -= GRAVITY * delta
	elif Input.is_action_just_pressed("fps_jump"):
		velocity.y = JUMP_SPEED
	else:
		velocity.y = 0.0
	# No background walking when the window loses focus or the cursor is released.
	if Input.mouse_mode != Input.MOUSE_MODE_CAPTURED or not get_window().has_focus():
		velocity.x = 0.0
		velocity.z = 0.0
		move_and_slide()
		return
	var axis := Input.get_vector("fps_left", "fps_right", "fps_forward", "fps_backward")
	var direction := (global_transform.basis * Vector3(axis.x, 0.0, axis.y)).normalized()
	var speed: float = RUN_SPEED if Input.is_action_pressed("fps_run") else WALK_SPEED
	velocity.x = direction.x * speed
	velocity.z = direction.z * speed
	move_and_slide()
	if Input.is_action_just_pressed("fps_inspect"):
		sight.force_raycast_update()
		var hit := sight.get_collider()
		if hit is CollisionObject3D and hit.has_meta("inspect_label"):
			inspected.emit(str(hit.get_meta("inspect_label")))
		else:
			inspected.emit("No object in interaction range")

func _notification(what: int) -> void:
	if what == NOTIFICATION_WM_WINDOW_FOCUS_OUT:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		# On focus loss, motion axes come exclusively from physical key state;
		# reset motion explicitly to avoid moving a ship/character behind another app.
		velocity.x = 0.0
		velocity.z = 0.0
