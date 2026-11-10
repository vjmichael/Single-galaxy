extends RefCounted
## FP3D-UX-01: guidance only, never an authorization or a world mutation.
## Position and eligibility come from the most recent independently admitted
## original native station PERSON receipt. A ghost/local Godot avatar is never
## sufficient to claim that boarding, refining or piloting is available.

# These are exact original C++ bounds, not approximate Godot collision sizes:
# Simulation::station_player_action(Board) checks each axis within +/-600 mm.
const NATIVE_HATCH_AXIS_MM := 600
# station_refinery_protocol::accessible checks a 1200mm circle at (3000,0).
const REFINERY_CENTER_X_MM := 3000
const REFINERY_RADIUS_MM := 1200

static func action_key(action: String, fallback: String) -> String:
	# Read real InputMap so the display remains correct if bindings change.
	if InputMap.has_action(action):
		for event in InputMap.action_get_events(action):
			if event is InputEventKey:
				var key: Key = event.physical_keycode
				if key != KEY_NONE:
					var name: String = OS.get_keycode_string(key)
					if not name.is_empty():
						return name
	return fallback

static func hint(station_active: bool, person: Dictionary, ship_active: bool,
		ship_seated: bool, dock_state: int, handoff_pending: bool,
		interact_key: String = "E") -> String:
	if handoff_pending:
		return "Connecting original world — waiting for the previous native writer to exit"
	if station_active:
		if person.is_empty():
			return "Loading original station position — interactions unavailable"
		var state: int = int(person.get("state", -1))
		if state == 4:
			var x: int = int(person.get("x_mm", 0))
			var z: int = int(person.get("z_mm", 0))
			var eligible: int = int(person.get("eligible_ship", 0))
			if eligible > 0 and absi(x) <= NATIVE_HATCH_AXIS_MM and absi(z) <= NATIVE_HATCH_AXIS_MM:
				return "Press %s — Board owned ship #%d at the glowing hatch" % [interact_key, eligible]
			var dx: int = x - REFINERY_CENTER_X_MM
			if dx * dx + z * z <= REFINERY_RADIUS_MM * REFINERY_RADIUS_MM:
				return "Press R — Use station refinery terminal"
			if eligible <= 0:
				return "No existing eligible docked ship — boarding unavailable"
			return "Find the CYAN BOARDING HATCH floor marker (%.1f m away)" % (Vector2(float(x), float(z)).length() / 1000.0)
		if state == 0:
			return "Press P — Enter your boarded ship's cockpit  |  %s — Disembark" % interact_key
		return "Station identity not ready — no interaction authorized"
	if ship_active:
		if not ship_seated:
			return "Approach the cockpit console, then press %s to request the pilot seat" % interact_key
		if dock_state == 1:
			return "Press U — Undock your original ship"
		if dock_state == 0:
			return "Up/Down — Thrust  |  V — Exterior camera  |  B — Brake  |  G — Dock nearby"
		return "Waiting for verified original ship docking status"
	return "Press H — Connect to the native station before boarding your ship"
