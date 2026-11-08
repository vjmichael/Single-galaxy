extends RefCounted
## Action-based keyboard defaults for on-foot and native cockpit pilot.
## Remapping/gamepad input is deliberately deferred; no raw ship keycodes in the controller.

const WALK_KEYS := {
	"fps_forward": KEY_W,
	"fps_backward": KEY_S,
	"fps_left": KEY_A,
	"fps_right": KEY_D,
	"fps_jump": KEY_SPACE,
	"fps_run": KEY_SHIFT,
	"fps_inspect": KEY_E,
}

const PILOT_KEYS := {
	"ship_yaw_left": KEY_A,
	"ship_yaw_right": KEY_D,
	"ship_strafe_port": KEY_W,
	"ship_strafe_starboard": KEY_S,
	"ship_thrust_forward": KEY_UP,
	"ship_thrust_reverse": KEY_DOWN,
	"ship_counterthrust": KEY_B,
	"ship_undock": KEY_U,
}

static func install() -> void:
	var actions := WALK_KEYS.duplicate()
	for key: String in PILOT_KEYS.keys():
		actions[key] = PILOT_KEYS[key]
	for action_name: String in actions.keys():
		if not InputMap.has_action(action_name):
			InputMap.add_action(action_name)
		var key_event := InputEventKey.new()
		key_event.physical_keycode = actions[action_name]
		if not InputMap.action_has_event(action_name, key_event):
			InputMap.action_add_event(action_name, key_event)

static func inspect_controls() -> Dictionary:
	return {"on_foot": WALK_KEYS.duplicate(true),
		"ship": PILOT_KEYS.duplicate(true),
		"authority": "Godot intent only; native cockpit C++ flight"}
