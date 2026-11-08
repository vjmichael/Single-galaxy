extends RefCounted
## Deliberate on-foot input map. Ship steering/propulsion belongs to a
## future *native-authorized* seat/pilot mode, never reused in walking.

const WALK_KEYS := {
	"fps_forward": KEY_W,
	"fps_backward": KEY_S,
	"fps_left": KEY_A,
	"fps_right": KEY_D,
	"fps_jump": KEY_SPACE,
	"fps_run": KEY_SHIFT,
	"fps_inspect": KEY_E,
}

static func install() -> void:
	for action_name: String in WALK_KEYS.keys():
		if not InputMap.has_action(action_name):
			InputMap.add_action(action_name)
		var key_event := InputEventKey.new()
		key_event.physical_keycode = WALK_KEYS[action_name]
		if not InputMap.action_has_event(action_name, key_event):
			InputMap.action_add_event(action_name, key_event)

static func inspect_controls() -> Dictionary:
	return {"on_foot": WALK_KEYS.duplicate(true),
		"ship_controls_reserved": [KEY_UP, KEY_DOWN, KEY_B],
		"authority": "local-graybox-only"}
