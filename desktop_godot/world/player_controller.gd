extends CharacterBody3D
## FP3D-02 prototype: local Godot walking ONLY. Not authoritative galaxy custody.
## No browser, no ship thrust, no persistence, no weapons or fake remote control.

signal inspected(display_name: String)
signal native_motion_intent(heading: Vector3)
signal native_seat_intent()
signal native_ship_intents(actions: PackedStringArray)
var native_seated := false
var native_dock_state := -1
var native_in_ship := false
var authoritative_mode := false
# On-foot Shift is a latch, not a hold-to-sprint requirement. Native C++
# remains the only movement authority; this controls legal step cadence.
var run_toggled := false

func toggle_run() -> void:
	if native_in_ship or native_seated:
		return
	run_toggled = not run_toggled

func use_native_ship_custody(aboard: bool) -> void:
	# This is a verified original PERSON/POSE presentation flag only.
	# Do not enable cabin walking between BOARD and native seat receipt.
	native_in_ship = aboard
	if aboard:
		velocity = Vector3.ZERO
# Physical mouse motion suggests yaw intent only; the actual 15-degree turn
# remains a per-command, journaled original C++ decision. There is NO client
# integration of a fake ship heading/velocity.
var _mouse_yaw_direction := 0
var _mouse_yaw_valid_until_ms := 0

func use_native_dock_state(dock_state: int) -> void:
	native_dock_state = dock_state


func use_native_authority(enabled: bool) -> void:
	authoritative_mode = enabled
	if not enabled:
		native_seated = false
		_mouse_yaw_direction = 0
	velocity = Vector3.ZERO

func use_native_seat(seated: bool) -> void:
	if native_seated != seated:
		_mouse_yaw_direction = 0
	native_seated = seated
	velocity = Vector3.ZERO

const WALK_SPEED := 5.0
const RUN_SPEED := 7.5
const JUMP_SPEED := 4.5
const MOUSE_SENSITIVITY := 0.0019
const MOUSE_STEER_THRESHOLD_PX := 2.5
const MOUSE_STEER_WINDOW_MS := 150

static func native_mouse_yaw_direction(relative_x: float) -> int:
	# Pure intent classification. The C++ original accepts/rejects the
	# resulting LEFT/RIGHT command under seat, epoch and resource custody.
	if relative_x > MOUSE_STEER_THRESHOLD_PX:
		return 1
	if relative_x < -MOUSE_STEER_THRESHOLD_PX:
		return -1
	return 0

const EYE_HEIGHT := 0.58
const GRAVITY := 17.0
const INPUT_CONFIG := preload("res://world/input_contract.gd")

var camera: Camera3D
var sight: RayCast3D

# Cosmetic camera interpolation is strictly behind an acknowledged native
# PERSON state. The player's authoritative Godot proxy position is snapped
# directly to every original C++ WALK receipt, never predicted or written.
const MAX_VISUAL_WALK_STEP_M := 0.20
const CAMERA_RECENTER_HZ := 18.0

func apply_native_station_pose(confirmed_position: Vector3, smooth_view: bool) -> void:
	var old_avatar: Vector3 = global_position
	var old_camera: Vector3 = camera.global_position if is_instance_valid(camera) else confirmed_position
	global_position = confirmed_position
	if not is_instance_valid(camera):
		return
	if smooth_view and old_avatar.distance_to(confirmed_position) <= MAX_VISUAL_WALK_STEP_M:
		# Retain the last displayed camera point, then ease only toward
		# the NEW already-confirmed position. Cannot move the physical actor.
		camera.global_position = old_camera
	else:
		# First login, restore from dock, or an unusually large real
		# positional discontinuity must never animate a fictitious journey.
		camera.position = Vector3(0.0, EYE_HEIGHT, 0.0)

func _process(delta: float) -> void:
	if authoritative_mode and not native_in_ship and is_instance_valid(camera) and camera.current:
		camera.position = camera.position.lerp(
			Vector3(0.0, EYE_HEIGHT, 0.0),
			clampf(delta * CAMERA_RECENTER_HZ, 0.0, 1.0))

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
	if event is InputEventKey and event.pressed and not event.echo and \
			(event.keycode == KEY_SHIFT or event.physical_keycode == KEY_SHIFT) and \
			get_window().has_focus() and not native_in_ship and not native_seated:
		toggle_run()
		get_viewport().set_input_as_handled()
	elif if event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_ESCAPE:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		get_viewport().set_input_as_handled()
	elif event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
		if Input.mouse_mode != Input.MOUSE_MODE_CAPTURED:
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
			get_viewport().set_input_as_handled()
	elif event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED and get_window().has_focus():
		if authoritative_mode and native_in_ship:
			# Boarding locks the entire cabin into a single pilot role.
			# Only a verified native SEAT receipt enables ship steering; no
			# speculative mouse look, boarding animation or on-foot exploration.
			if not native_seated:
				get_viewport().set_input_as_handled()
				return
			# The pilot mouse requests an original C++ yaw action instead of
			# rotating a disconnected Godot cockpit camera.
			var requested_yaw := native_mouse_yaw_direction(event.relative.x)
			if requested_yaw != 0:
				_mouse_yaw_direction = requested_yaw
				_mouse_yaw_valid_until_ms = Time.get_ticks_msec() + MOUSE_STEER_WINDOW_MS
		else:
			rotate_y(-event.relative.x * MOUSE_SENSITIVITY)
		camera.rotation.x = clamp(camera.rotation.x - event.relative.y * MOUSE_SENSITIVITY, -1.45, 1.45)
		get_viewport().set_input_as_handled()

func _collect_native_pilot_actions() -> PackedStringArray:
	# User input is only a proposal; C++ owns the authorization and movement.
	# Keep this separable for real-Godot action-press tests with cursor released.
	var denied := PackedStringArray()
	if not authoritative_mode or not native_in_ship or not native_seated:
		return denied
	var pilot_actions := PackedStringArray()
	if native_dock_state == 1 and Input.is_action_pressed("ship_undock"):
		pilot_actions.append("UND")
	# Hold G for one available native command slot; just_pressed
	# could be lost while a prior 110ms flight request was pending.
	# Only ask in space; the C++ host still checks approach speed/range.
	if native_dock_state == 0 and Input.is_action_pressed("ship_dock"):
		pilot_actions.append("DCK")
	if Input.is_action_pressed("ship_counterthrust"):
		pilot_actions.append("BRAKE")
	if Input.is_action_pressed("ship_yaw_left"):
		pilot_actions.append("LEFT")
	if Input.is_action_pressed("ship_yaw_right"):
		pilot_actions.append("RIGHT")
	# Mouse steering is an alternative to A/D, not an extra command
	# source that fights an explicitly held keyboard steering axis.
	if Input.mouse_mode == Input.MOUSE_MODE_CAPTURED and \
			not Input.is_action_pressed("ship_yaw_left") and not Input.is_action_pressed("ship_yaw_right"):
		if Time.get_ticks_msec() <= _mouse_yaw_valid_until_ms:
			if _mouse_yaw_direction < 0:
				pilot_actions.append("LEFT")
			elif _mouse_yaw_direction > 0:
				pilot_actions.append("RIGHT")
		else:
			_mouse_yaw_direction = 0
	if Input.is_action_pressed("ship_strafe_port"):
		pilot_actions.append("PORT")
	if Input.is_action_pressed("ship_strafe_starboard"):
		pilot_actions.append("STAR")
	if Input.is_action_pressed("ship_thrust_forward"):
		pilot_actions.append("FWD")
	if Input.is_action_pressed("ship_thrust_reverse"):
		pilot_actions.append("REV")
	# Only native v119+ original C++ may apply actual vertical velocity.
	# Godot does not move or elevate the visible ship locally.
	if Input.is_action_pressed("ship_ascend"):
		pilot_actions.append("ASC")
	if Input.is_action_pressed("ship_descend"):
		pilot_actions.append("DES")
	return pilot_actions

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
		# Keyboard pilot inputs need an active foreground window, not cursor
		# capture. Esc or an OS/UI cursor release must never immobilize a
		# verified native pilot. Relative mouse yaw still requires capture.
		if get_window().has_focus():
			if native_seated and native_in_ship:
				# Only intentional physical control requests. C++ retains ship motion,
				# yaw, thrust admission and persistent world authority.
				var pilot_actions := _collect_native_pilot_actions()
				if not pilot_actions.is_empty():
					native_ship_intents.emit(pilot_actions)
			elif not native_in_ship and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
				# Walking still requires captured mouse and original PERSON custody.
				var axis := Input.get_vector("fps_left", "fps_right", "fps_forward", "fps_backward")
				var direction := (global_transform.basis * Vector3(axis.x, 0.0, axis.y)).normalized()
				native_motion_intent.emit(direction)
			# Aboard but awaiting SEAT: native walk is intentionally impossible.
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
	var speed: float = RUN_SPEED if run_toggled else WALK_SPEED
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
		_mouse_yaw_direction = 0
		_mouse_yaw_valid_until_ms = 0
		velocity.x = 0.0
		velocity.z = 0.0
