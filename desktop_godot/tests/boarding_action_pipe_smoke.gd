extends SceneTree
## FP3D-UX-02: real Godot bridge state machine. The typed native WALK
## acknowledgement may arrive AFTER E. We must not silently drop E, invent
## boarding custody or send BOARD with a stale SQLite generation.
## This test's file-backed stdout is a synthetic PIPE stand-in, never a
## substitute for original C++/SGDX/SQLite native boarding acceptance.
const STATION := preload("res://world/native_station_session.gd")

func _initialize() -> void:
	call_deferred("_run")

func _fail(message: String) -> void:
	push_error("FP3D-UX-02 boarding intent smoke FAIL: " + message)
	quit(1)

func _setup(person_x: int, pending_walk: bool) -> Variant:
	var session := STATION.new()
	session._active = true
	session._session = 77
	session._generation = 2
	session._tick = 1
	session._next_request = 10
	session._pending = 9 if pending_walk else 0
	session._pending_walk = 9 if pending_walk else 0
	session._person = {"state":4, "station":1, "ship":0,
		"x_mm":person_x,"z_mm":0,"eligible_ship":5,
		"receipt":43,"generation":2,"tick":1}
	return session

func _run() -> void:
	var path: String = "user://boarding_action_pipe_smoke.txt"
	var session = _setup(690, true)
	session._stdout = FileAccess.open(path, FileAccess.WRITE)
	if session._stdout == null:
		_fail("cannot create read-only simulation of pipe writer")
		return
	session.interact_hangar()
	session.interact_hangar() # repeated E before one WALK receipt is bounded
	if not session._queued_hangar_intent or session._pending != 9:
		_fail("E pressed during pending WALK lost or generated early BOARD")
		return
	# Original host confirms this exact WALK moved the registered PERSON into
	# the server's square 600mm hatch bound and advanced generation.
	session._accept_line("PERSON|2|77|9|2|3|4|1|0|600|0|5|44")
	if session._pending != 0 or not session._queued_hangar_intent or session._pending_walk != 0 or session._queued_hangar_ready_msec <= Time.get_ticks_msec():
		_fail("fresh PERSON must defer E until original C++ command cooldown, not send immediate rate-DENIED BOARD")
		return
	session._dispatch_queued_hangar_if_ready()
	if session._pending != 0:
		_fail("E sent BEFORE native command rate interval")
		return
	await create_timer(0.18).timeout
	session._dispatch_queued_hangar_if_ready()
	if session._pending != 10 or session._queued_hangar_intent:
		_fail("one queued E was not issued after the native cooldown")
		return
	session._stdout.close()
	var sent: String = FileAccess.get_file_as_string(path).strip_edges()
	if sent != "BOARD|77|10|3|5":
		_fail("BOARD pipe is not exact one-use native request with CURRENT generation: " + sent)
		return
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
	# E immediately after an ALREADY acknowledged WALK must also be held,
	# rather than hitting the original 45ms C++ rate rejection.
	var idle = _setup(600, false)
	idle._stdout = FileAccess.open(path, FileAccess.WRITE)
	if idle._stdout == null:
		_fail("could not create fresh synthetic protocol pipe")
		return
	idle._last_send = Time.get_ticks_msec()
	idle.interact_hangar()
	if idle._pending != 0 or not idle._queued_hangar_intent:
		_fail("immediate E ignored real C++ cooldown on idle connection")
		return
	idle._dispatch_queued_hangar_if_ready()
	if idle._pending != 0:
		_fail("idle E sent before native 130ms gap")
		return
	await create_timer(0.18).timeout
	idle._dispatch_queued_hangar_if_ready()
	if idle._pending != 10:
		_fail("deferred idle E was not dispatched once after cooldown")
		return
	idle._stdout.close()
	if FileAccess.get_file_as_string(path).strip_edges() != "BOARD|77|10|2|5":
		_fail("deferred original BOARD used stale or incorrect native generation")
		return
	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
	var far = _setup(601, false)
	far.interact_hangar()
	if far._pending != 0 or far._queued_hangar_intent:
		_fail("E sent optimistic BOARD outside actual 600mm native hatch")
		return
	var denied = _setup(690, true)
	denied.interact_hangar()
	denied._accept_line("DENY|2|9|station-custody-or-distance")
	if denied._queued_hangar_intent or denied._pending_walk != 0 or denied._pending != 0:
		_fail("rejected WALK replayed previously queued boarding intent")
		return
	var no_host = STATION.new()
	no_host.interact_hangar()
	if no_host._queued_hangar_intent:
		_fail("E manufactured a native BOARD without original supervisor")
		return
	print("FP3D-UX-03 Godot boarding intent PASS: native WALK/E and immediate-idle E each delayed past original C++ 45ms rate throttle, fresh PERSON generation, one-use BOARD, no denial replay")
	quit(0)
