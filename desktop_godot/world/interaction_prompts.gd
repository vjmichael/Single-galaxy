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
				var key: int = event.physical_keycode
				if key != KEY_NONE:
					var name: String = OS.get_keycode_string(key)
					if not name.is_empty():
						return name
	return fallback

static func hatch_direction_key(person: Dictionary, player_basis: Basis) -> String:
	# Convert the authenticated native station displacement into the SAME
	# locally oriented WASD frame used to submit trusted C++ WALK requests.
	# This is guidance only: no movement/key injection or source-world writes.
	if person.is_empty() or int(person.get("state", -1)) != 4:
		return ""
	var x_mm: int = int(person.get("x_mm", 0))
	var z_mm: int = int(person.get("z_mm", 0))
	if absi(x_mm) <= NATIVE_HATCH_AXIS_MM and absi(z_mm) <= NATIVE_HATCH_AXIS_MM:
		return ""
	var toward: Vector3 = Vector3(-float(x_mm), 0.0, -float(z_mm))
	var local: Vector3 = player_basis.inverse() * toward
	if absf(local.x) > absf(local.z):
		return "D" if local.x > 0 else "A"
	return "S" if local.z > 0 else "W"

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
				return "Press %s to Board  |  Owned ship #%d at colony departure dock" % [interact_key, eligible]
			var dx: int = x - REFINERY_CENTER_X_MM
			if dx * dx + z * z <= REFINERY_RADIUS_MM * REFINERY_RADIUS_MM:
				return "Press R — Use station refinery terminal"
			if eligible <= 0:
				return "No existing eligible docked ship — boarding unavailable"
			return "COLONY → DOCK  |  Follow cyan boarding walkway (%.1f m)" % (Vector2(float(x), float(z)).length() / 1000.0)
		if state == 0:
			return "Boarding complete — opening your ship cockpit for departure"
		return "Station identity not ready — no interaction authorized"
	if ship_active:
		if not ship_seated:
			return "Automatic pilot seating — awaiting original C++ SEAT receipt"
		if dock_state == 1:
			return "THIRD-PERSON PILOT | U undock  •  A/D or mouse turn  •  W/S vertical"
		if dock_state == 0:
			# No cabin walking, free-look toggle, client miner or fake ore.
			return "THIRD-PERSON FLIGHT | A/D turn • W/S ascend/descend • ↑/↓ forward/reverse • B brake • G dock\nMINING LOCKED — original C++ equipment not fitted yet"
		return "Waiting for verified original ship docking status"
	return "Connecting to your original saved station automatically..."
