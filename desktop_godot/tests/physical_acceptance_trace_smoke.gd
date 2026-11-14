extends SceneTree
## Godot file/trace contract smoke only; never claims physical R9700 acceptance.
const TRACE := preload("res://world/physical_acceptance_diagnostics.gd")
const MAIN := preload("res://world/main.gd")
const NATIVE := preload("res://world/native_player_session.gd")

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var folder := OS.get_user_data_dir().path_join("physical-evidence-smoke-" + str(Time.get_ticks_usec()))
	var trace = TRACE.new()
	trace.begin(folder, "0123456789abcdef0123456789abcdef01234567")
	if not trace.enabled():
		_fail("trace did not create isolated evidence file")
		return
	trace.record("original_flight_submitted", {"request_id":17,"verb":"FWD","cursor_captured":false})
	trace.record("original_flight_result", {"request_id":17,"verb":"FWD",
		"result":"ACK","ack_latency_ms":133,"native_outcome":0})
	trace.pacing({"metric":"Godot main-loop frame interval (NOT GPU render time)",
		"samples":3,"p95_ms":18.0},{"ship_tick":52,"cursor_captured":false})
	trace.finish()
	var dir := DirAccess.open(folder)
	if dir == null:
		_fail("trace output directory cannot be read")
		return
	var trace_files := PackedStringArray()
	for name in dir.get_files():
		if name.begins_with("physical-session-") and name.ends_with(".jsonl"):
			trace_files.append(name)
	if trace_files.size() != 1:
		_fail("expected exactly one launch-scoped trace, got %d" % trace_files.size())
		return
	var file := FileAccess.open(folder.path_join(trace_files[0]), FileAccess.READ)
	if file == null:
		_fail("readback failed")
		return
	var events := PackedStringArray()
	while not file.eof_reached():
		var line := file.get_line()
		if line.is_empty():
			continue
		var parsed = JSON.parse_string(line)
		if not (parsed is Dictionary) or parsed.get("schema", "") != "SG-FP3D-PHYSICAL-TRACE-01":
			_fail("invalid structured trace readback")
			return
		events.append(str(parsed.get("event", "")))
	file.close()
	for expected in ["session_begin", "original_flight_submitted",
			"original_flight_result", "rolling_frame_pacing", "session_end"]:
		if not events.has(expected):
			_fail("missing trace event: " + expected)
			return
	var main = MAIN.new()
	var native = NATIVE.new()
	if not main.has_method("_on_physical_flight_sent") or \
			not main.has_method("_on_physical_flight_result") or \
			not native.has_signal("flight_command_sent") or \
			not native.has_signal("flight_command_result"):
		_fail("live Godot/native evidence signal interface missing")
		return
	print("FP3D-R9700 EVIDENCE CONTRACT PASS: source signals, scoped structured trace, command pairing and rolling timing; NOT a physical GPU pass")
	quit(0)

func _fail(reason: String) -> void:
	push_error("FP3D-R9700 EVIDENCE CONTRACT FAILED: " + reason)
	quit(1)
