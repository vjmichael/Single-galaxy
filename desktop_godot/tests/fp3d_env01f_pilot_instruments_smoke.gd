extends SceneTree
## FP3D-ENV-01F synthetic UI-logic test only. Actual SHIP/ROCK admission,
## real OS child, SQLite and visible camera have separate native gates.
const INSTRUMENTS := preload("res://world/native_flight_instruments.gd")
var failures: Array[String] = []

func _check(condition: bool, reason: String) -> void:
	if not condition:
		failures.append(reason)
		push_error("FP3D-ENV-01F " + reason)

func _initialize() -> void:
	var ship := {"ship":7, "dock":0, "tick":21, "gen":4,
		"heading":0, "vx":500, "vy":0, "vz":0}
	var rock := {"found":true, "ship":7, "tick":21, "generation":4,
		"rock":14, "dx_mm":15000, "dy_mm":0, "dz_mm":0,
		"radius_mm":1000, "impact_rock":0, "impact_tick":0,
		"cargo_tonnes":2, "cargo_capacity":24, "mining_tool_online":0}
	var r: Dictionary = INSTRUMENTS.from_verified_frames(ship, rock)
	_check(bool(r.get("valid", false)) and bool(r.get("found", false)),
		"matching native tick and owner should allow read-only projection")
	_check(absf(float(r.get("clearance_m", -1)) - 8.799) < 0.001,
		"range must exclude original 5.2m ship hull, 1m rock and 1mm skin")
	_check(absf(float(r.get("radial_rate_mm_tick", 0)) - 500.0) < 0.001,
		"native positive radial velocity must show closing speed")
	_check(INSTRUMENTS.display(r).contains("AHEAD") and
		INSTRUMENTS.display(r).contains("CLOSING 500"),
		"matching approach should be visible without simulated motion")
	var side: Dictionary = rock.duplicate(true)
	side["dx_mm"] = 0
	side["dy_mm"] = 20000
	side["dz_mm"] = 2000
	r = INSTRUMENTS.from_verified_frames(ship, side)
	_check(absf(float(r.get("bearing_deg", 0)) - 90.0) < 0.01 and
		INSTRUMENTS.display(r).contains("RIGHT") and
		INSTRUMENTS.display(r).contains("ABOVE"),
		"native +Y and +Z must map to correct pilot bearing and elevation")
	var backing: Dictionary = ship.duplicate(true)
	backing["vx"] = -400
	r = INSTRUMENTS.from_verified_frames(backing, rock)
	_check(INSTRUMENTS.display(r).contains("RECEDING 400"),
		"negative native radial velocity is receding, not closing")
	var impact: Dictionary = rock.duplicate(true)
	impact["impact_rock"] = 14
	impact["impact_tick"] = 21
	r = INSTRUMENTS.from_verified_frames(ship, impact)
	_check(bool(r.get("confirmed_contact", false)) and
		INSTRUMENTS.display(r).contains("VERIFIED IMPACT"),
		"only matching original rock/tick may display verified collision")
	var wrong_rock: Dictionary = impact.duplicate(true)
	wrong_rock["impact_rock"] = 15
	r = INSTRUMENTS.from_verified_frames(ship, wrong_rock)
	_check(not bool(r.get("confirmed_contact", true)),
		"unrelated impact ID must not be reported against selected rock")
	var stale: Dictionary = rock.duplicate(true)
	stale["tick"] = 20
	_check(INSTRUMENTS.from_verified_frames(ship, stale).is_empty(),
		"stale ROCK must not join current SHIP")
	stale = rock.duplicate(true)
	stale["generation"] = 3
	_check(INSTRUMENTS.from_verified_frames(ship, stale).is_empty(),
		"stale SQLite generation must not join current SHIP")
	stale = rock.duplicate(true)
	stale["ship"] = 8
	_check(INSTRUMENTS.from_verified_frames(ship, stale).is_empty(),
		"other owner's ship must never produce a target instrument")
	var docked: Dictionary = ship.duplicate(true)
	docked["dock"] = 1
	_check(INSTRUMENTS.from_verified_frames(docked, rock).is_empty(),
		"no space target from a docked C++ ship")
	var missing: Dictionary = rock.duplicate(true)
	missing["found"] = false
	r = INSTRUMENTS.from_verified_frames(ship, missing)
	_check(bool(r.get("valid", false)) and not bool(r.get("found", true)) and
		INSTRUMENTS.display(r).contains("no verified rock within 30m"),
		"ROCKNONE must not synthesize an asteroid or range")
	var invalid: Dictionary = rock.duplicate(true)
	invalid["dx_mm"] = 0
	invalid["dy_mm"] = 0
	invalid["dz_mm"] = 0
	_check(INSTRUMENTS.from_verified_frames(ship, invalid).is_empty(),
		"impossible zero-range target must fail closed")
	_check(INSTRUMENTS.from_verified_frames({}, rock).is_empty(),
		"no native SHIP means no target")
	if not failures.is_empty():
		quit(1)
	else:
		print("FP3D-ENV-01F pilot instruments PASS: 14 synthetic read-only native-receipt checks; engine/runtime/GPU gates still open")
		quit(0)
