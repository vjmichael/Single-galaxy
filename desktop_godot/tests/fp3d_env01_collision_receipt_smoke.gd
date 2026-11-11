extends SceneTree
## Synthetic parser only; real original C++ is separately exercised by CTest.
const SESSION := preload("res://world/native_player_session.gd")
const SCENE := preload("res://world/main.gd")
func _initialize() -> void:
	var s = SESSION.new()
	var receipts := []
	s.asteroid_contact_received.connect(func(v: Dictionary): receipts.append(v))
	s._accept_line("POSE|1|321|0|0|0|0|0|10|2|7|1|1")
	s._accept_line("SHIP|1|321|0|0|0|7|10|2|0|0|0|0|0|0|0|0")
	s._accept_line("ROCK|1|321|0|7|10|2|11|3770|23000|0|0|10000|2|0|24|0|0|0")
	if receipts.size()!=1 or int(receipts[0]["impact_rock"])!=0:
		push_error("expected no new contact on initial snapshot")
		quit(1)
		return
	s._pending=1
	s._pending_kind="FLIGHT"
	s._accept_line("SHIP|1|321|1|1152921504606846977|0|7|11|3|11500|0|0|0|0|0|0|0")
	s._accept_line("ROCK|1|321|1|7|11|3|11|3770|13500|0|0|10000|2|0|24|0|11|11")
	if receipts.size()!=2 or int(receipts[1]["impact_rock"])!=11 or int(receipts[1]["impact_tick"])!=11:
		push_error("native collision receipt lost its original rock/tick")
		quit(1)
		return
	# Original yaw: 0 degrees advances +native X / +Godot X;
	# 90 degrees advances +native Y / -Godot Z. The exterior
	# camera rides the verified hull, rather than free mouse yaw.
	var hull_0 := Basis(Vector3.UP, SCENE.native_heading_to_godot_yaw(0))
	var hull_90 := Basis(Vector3.UP, SCENE.native_heading_to_godot_yaw(90))
	if (hull_0 * Vector3.RIGHT).distance_to(Vector3.RIGHT)>0.0001 or \
		(hull_90 * Vector3.RIGHT).distance_to(Vector3.FORWARD)>0.0001:
		push_error("native ship yaw does not match Godot exterior geometry")
		quit(1)
		return
	var pos := SCENE.native_rock_visual_offset({"dx_mm":25000,"dy_mm":-2000,"dz_mm":3000})
	if pos.distance_to(Vector3(25,3,2))>0.0001:
		push_error("incorrect native millimetres-to-Godot axis mapping")
		quit(1)
		return
	var bad = SESSION.new()
	var ignored := []
	bad.asteroid_contact_received.connect(func(v: Dictionary): ignored.append(v))
	bad._accept_line("POSE|1|321|0|0|0|0|0|10|2|7|1|1")
	bad._accept_line("SHIP|1|321|0|0|0|7|10|2|0|0|0|0|0|0|0|0")
	bad._accept_line("ROCK|1|321|0|7|10|2|11|3770|23000|0|0|10000|2|0|24|0|11|9")
	if not ignored.is_empty():
		push_error("accepted unverified impact tick")
		quit(1)
		return
	print("FP3D-ENV-01 Godot parser/3D metric projection PASS (synthetic; not Windows gameplay)")
	quit(0)
