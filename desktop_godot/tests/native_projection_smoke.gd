extends SceneTree
## Runs in official Godot, once installed: --headless --script res://tests/native_projection_smoke.gd
const NATIVE := preload("res://world/native_projection.gd")
var accepted: Array[Dictionary] = []
var errors: Array[String] = []

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var p = NATIVE.new()
	p.snapshot_received.connect(func(frame: Dictionary): accepted.append(frame))
	p.rejected.connect(func(message: String): errors.append(message))
	var first := "SGDESK|1|18446744073709551615|0|3|1|2|7|0|0|0|0|2\nCONTACT|2|0|7|0|0|0\nCONTACT|3|1|0|2500|-700|40\nEND\n"
	# Test buffering over byte-split pipes.
	p.accept_bytes(first.substr(0, 30).to_utf8_buffer())
	p.accept_bytes(first.substr(30).to_utf8_buffer())
	if accepted.size() != 1 or accepted[0]["contacts"].size() != 2 or not errors.is_empty():
		_fail("valid complete chunked native snapshot")
		return
	var second := "SGDESK|1|18446744073709551615|1|4|1|2|7|0|0|0|0|1\nCONTACT|2|0|7|0|0|0\nEND\n"
	p.accept_bytes(second.to_utf8_buffer())
	if accepted.size() != 2:
		_fail("monotonic native tick and sequence")
		return
	p.accept_bytes(second.to_utf8_buffer())
	if errors.size() != 1:
		_fail("duplicate sequence must be rejected")
		return
	p.reset()
	var forged := "SGDESK|1|1|0|1|1|2|7|0|0|0|0|1\nCONTACT|2|0|0|0|0|0\nEND\n"
	p.accept_bytes(forged.to_utf8_buffer())
	if errors.size() != 2:
		_fail("masked foreign ship cannot be the cockpit")
		return
	print("FP3D-02B GODOT parser PASS: fragmented stream, U64 epoch, replay rejection, owner identity")
	quit(0)

func _fail(reason: String) -> void:
	push_error("FP3D-02B GODOT parser FAILED: " + reason)
	quit(1)
