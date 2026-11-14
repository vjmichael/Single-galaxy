extends RefCounted
## DISPLAY ONLY: nearest same-frame station reported by original C++ in a
## matching SHIP-tick navigation packet. Does not grant docking or motion.

const DOCK_RANGE_M := 2.5

static func from_verified_frames(ship: Dictionary, nav: Dictionary) -> Dictionary:
	if ship.is_empty() or nav.is_empty() or \
			int(nav.get("ship", -1)) != int(ship.get("ship", -2)) or \
			int(nav.get("tick", -1)) != int(ship.get("tick", -2)) or \
			int(nav.get("gen", -1)) != int(ship.get("gen", -2)):
		return {}
	var readout := {"verified":true,"found":bool(nav.get("found", false))}
	if not readout["found"]:
		return readout
	var station := int(nav.get("station", 0))
	var dx := float(nav.get("dx_mm", 0))
	var dy := float(nav.get("dy_mm", 0))
	var dz := float(nav.get("dz_mm", 0))
	var offset := Vector3(dx, dy, dz)
	if station <= 0 or offset.length() > 1000000000.0:
		return {}
	var bearing := wrapf(rad_to_deg(atan2(dy, dx)) -
		float(ship.get("heading", 0)), -180.0, 180.0)
	var speed := Vector3(float(ship.get("vx", 0)),float(ship.get("vy", 0)),
		float(ship.get("vz", 0))).length()
	return {
		"verified":true,"found":true,"station":station,
		"range_m":offset.length()/1000.0,"bearing_deg":bearing,
		"height_m":dz/1000.0,"speed_mm_tick":speed,
		# Verified original relative coordinates retained for VISUAL-ONLY
		# distant station rendering; no Godot steering or docking authority.
		"dx_mm":int(nav["dx_mm"]),"dy_mm":int(nav["dy_mm"]),"dz_mm":int(nav["dz_mm"]),
		"can_attempt_dock":offset.length() <= DOCK_RANGE_M*1000.0 and speed == 0.0 and
			int(ship.get("dock", -1)) == 0
	}

static func display(readout: Dictionary) -> String:
	if readout.is_empty():
		return "RETURN NAV | Waiting for original C++ station coordinates"
	if not bool(readout.get("found", false)):
		return "RETURN NAV | No original same-frame station in 1000 km sensor range"
	var angle := float(readout["bearing_deg"])
	var direction := "AHEAD"
	if absf(angle) >= 5.0:
		direction = "LEFT (A)" if angle > 0.0 else "RIGHT (D)"
	var altitude := float(readout["height_m"])
	var vertical := "LEVEL"
	if absf(altitude) >= 0.5:
		vertical = "ABOVE" if altitude > 0.0 else "BELOW"
	var approach := "G to request native dock" if bool(readout["can_attempt_dock"]) else \
		"Approach within 2.5m; stop completely with B before G"
	return "NEAREST STATION #%d | %.1fm | %s %.0f° | %s %.1fm | %s" % [
		int(readout["station"]),float(readout["range_m"]),direction,
		absf(angle),vertical,absf(altitude),approach]
