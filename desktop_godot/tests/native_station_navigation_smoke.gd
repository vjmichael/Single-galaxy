extends SceneTree
## Source/protocol regression only: no GPU claim and no fake game authority.
const NAV := preload("res://world/native_station_navigation.gd")
const SESSION := preload("res://world/native_player_session.gd")
const MAIN := preload("res://world/main.gd")

func _initialize() -> void:
	var s := SESSION.new()
	var seen: Array[Dictionary] = []
	s.station_nav_received.connect(func(n: Dictionary): seen.append(n))
	s._accept_line("POSE|1|123|0|0|0|0|0|0|1|2|0|0")
	s._accept_line("SHIP|1|123|0|0|0|2|0|1|0|0|0|0|0|0|0|1")
	s._accept_line("ROCKNONE|1|123|0|2|0|1|0|0|0")
	s._accept_line("NAV|1|123|0|2|0|1|77|2000|0|0")
	if seen.size() != 1 or not bool(seen[0]["found"]) or int(seen[0]["station"]) != 77:
		_fail("initial matched original C++ station navigation was not parsed")
		return
	var ship := {"ship":2,"tick":0,"gen":1,"heading":0,"dock":0,
		"vx":0,"vy":0,"vz":0}
	var nav := NAV.from_verified_frames(ship, seen[0])
	if nav.is_empty() or not bool(nav.get("can_attempt_dock", false)) or \
			absf(float(nav["range_m"])-2.0)>0.001:
		_fail("verified 2m stationary docking envelope was miscomputed")
		return
	if not NAV.display(nav).contains("G to request native dock"):
		_fail("actual in-range docking route omitted from pilot UI")
		return
	var moving_ship := ship.duplicate(true)
	moving_ship["vx"] = 250
	var moving := NAV.from_verified_frames(moving_ship, seen[0])
	if bool(moving.get("can_attempt_dock", true)):
		_fail("moving ship was shown as eligible for stationary docking")
		return
	var far_nav := seen[0].duplicate(true)
	far_nav["dx_mm"]=30000
	var far := NAV.from_verified_frames(ship, far_nav)
	if float(far.get("range_m", 0.0)) < 29.9 or \
			not NAV.display(far).contains("Approach within 2.5m"):
		_fail("distant station did not retain original range guidance")
		return
	var stale := seen[0].duplicate(true)
	stale["gen"]=2
	if not NAV.from_verified_frames(ship, stale).is_empty():
		_fail("stale native station navigation accepted for a different generation")
		return
	var left := seen[0].duplicate(true)
	left["dx_mm"]=0
	left["dy_mm"]=2000
	var direction := NAV.from_verified_frames(ship, left)
	if not NAV.display(direction).contains("LEFT (A) 90"):
		_fail("original X/Y station bearing mapped to the wrong yaw direction")
		return
	var none := seen[0].duplicate(true)
	none["found"]=false
	if not NAV.display(NAV.from_verified_frames(ship, none)).contains("No original same-frame station"):
		_fail("missing native station falsely presented as an original target")
		return
	var ui := MAIN.new()
	if not ui.has_method("_on_native_station_nav"):
		_fail("real game did not connect original station-nav display")
		return
	print("FP3D-R9700 RETURN NAV PASS: original SHIP/NAV identity, range, yaw, stop/dock guidance, stale rejection; not a physical GPU pass")
	quit(0)

func _fail(reason: String) -> void:
	push_error("FP3D-R9700 RETURN NAV FAILED: "+reason)
	quit(1)
