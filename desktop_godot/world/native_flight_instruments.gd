extends RefCounted
## FP3D-ENV-01F: read-only pilot instrumentation from a MATCHED original
## C++ SHIP + ROCK receipt. It never authorizes movement, mining or contact.
## Original ship X/Y/Z and velocity are millimetres and millimetres per tick.

const SHIP_PROXY_RADIUS_MM := 5200.0
const CONTACT_SKIN_MM := 1.0
const MIN_VECTOR_MM := 0.001

static func from_verified_frames(ship: Dictionary, rock: Dictionary) -> Dictionary:
	# The native pipe has already checked sequence, owner, generation and
	# monotonic tick. Re-check the join here to prevent a UI from projecting
	# a previous tick's target beside a newer ship.
	if ship.is_empty() or rock.is_empty():
		return {}
	if int(ship.get("ship", -1)) <= 0 or int(ship.get("dock", -1)) != 0 or \
			int(rock.get("ship", -2)) != int(ship["ship"]) or \
			int(rock.get("tick", -2)) != int(ship.get("tick", -1)) or \
			int(rock.get("generation", -2)) != int(ship.get("gen", -1)):
		return {}
	var speed_mm_tick := Vector3(float(ship.get("vx", 0)),
		float(ship.get("vy", 0)), float(ship.get("vz", 0))).length()
	var output := {
		"valid": true, "found": false,
		"ship": int(ship["ship"]), "tick": int(ship["tick"]),
		"heading": int(ship.get("heading", 0)),
		"speed_mm_tick": speed_mm_tick,
		"cargo_tonnes": int(rock.get("cargo_tonnes", 0)),
		"cargo_capacity": int(rock.get("cargo_capacity", 0)),
		"tool_online": int(rock.get("mining_tool_online", 0)) == 1
	}
	if not bool(rock.get("found", false)):
		return output
	var target_id := int(rock.get("rock", 0))
	var target_delta := Vector3(float(rock.get("dx_mm", 0)),
		float(rock.get("dy_mm", 0)), float(rock.get("dz_mm", 0)))
	var distance_mm := target_delta.length()
	var rock_radius_mm := float(rock.get("radius_mm", 0))
	if target_id <= 0 or distance_mm < MIN_VECTOR_MM or rock_radius_mm <= 0.0:
		return {} # refuse impossible target, never synthesize a zero-distance bearing
	var heading_deg := float(ship.get("heading", 0))
	# With Godot native +Y mapped to visual -Z, positive relative bearing
	# points LEFT of the displayed third-person ship, never RIGHT.
	var bearing_deg := wrapf(rad_to_deg(atan2(target_delta.y, target_delta.x)) -
		heading_deg, -180.0, 180.0)
	var radial_rate_mm_tick := Vector3(float(ship.get("vx", 0)),
		float(ship.get("vy", 0)), float(ship.get("vz", 0))).dot(target_delta) / distance_mm
	var clearance_mm := maxf(0.0, distance_mm - rock_radius_mm -
		SHIP_PROXY_RADIUS_MM - CONTACT_SKIN_MM)
	var confirmed_contact := int(rock.get("impact_rock", 0)) == target_id and \
		int(rock.get("impact_tick", 0)) == int(ship["tick"])
	output.merge({
		"found": true, "rock": target_id,
		"range_m": distance_mm / 1000.0,
		"clearance_m": clearance_mm / 1000.0,
		"bearing_deg": bearing_deg,
		"height_m": target_delta.z / 1000.0,
		"radial_rate_mm_tick": radial_rate_mm_tick,
		"confirmed_contact": confirmed_contact,
	}, true)
	return output

static func display(readout: Dictionary) -> String:
	if readout.is_empty():
		return "FLIGHT SENSOR: awaiting matching original SHIP + ROCK receipts"
	var speed := float(readout["speed_mm_tick"])
	if not bool(readout["found"]):
		return "NATIVE FLIGHT | yaw %d° | SPEED %.0f mm/tick | no verified rock within 30m" % [
			int(readout["heading"]), speed]
	var relative_bearing := float(readout["bearing_deg"])
	var turn := "AHEAD"
	if absf(relative_bearing) >= 5.0:
		turn = "LEFT" if relative_bearing > 0.0 else "RIGHT"
	var relative_height := float(readout["height_m"])
	var vertical := "LEVEL"
	if absf(relative_height) >= 0.5:
		vertical = "ABOVE" if relative_height > 0.0 else "BELOW"
	var rate := float(readout["radial_rate_mm_tick"])
	var approach := "CLOSING" if rate > 0.5 else ("RECEDING" if rate < -0.5 else "STATIONARY")
	var impact := " | VERIFIED IMPACT" if bool(readout["confirmed_contact"]) else ""
	return "TARGET #%d | %s %.0f° / %s %.1fm | hull clearance %.2fm | %s %.0f mm/tick%s" % [
		int(readout["rock"]), turn, absf(relative_bearing), vertical,
		absf(relative_height), float(readout["clearance_m"]),
		approach, absf(rate), impact]
