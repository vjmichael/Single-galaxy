extends RefCounted
## Opt-in physical player evidence: observations only. Never writes the
## authoritative SQLite world, sends game commands or captures desktop input.
## Enabled exclusively by the verified Windows package launcher environment.

const BUFFER_LIMIT := 64
const FILE_LIMIT_BYTES := 4194304
const PACING_INTERVAL_MS := 2000
var _file_path := ""
var _buffer := PackedStringArray()
var _bytes_written := 0
var _last_pacing_ms := 0
var _limit_reached := false

func enabled() -> bool:
	return not _file_path.is_empty()

func begin(directory: String, source_sha: String) -> void:
	if directory.is_empty() or DirAccess.make_dir_recursive_absolute(directory) != OK:
		return
	# Unique per-launch trace; previous operator evidence is never overwritten.
	var stamp := "%d-%d" % [int(Time.get_unix_time_from_system()), Time.get_ticks_usec()]
	var path := directory.path_join("physical-session-" + stamp + ".jsonl")
	if FileAccess.file_exists(path):
		return
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return
	f.close()
	_file_path = path
	record("session_begin", {"source_sha":source_sha,
		"scope":"game viewport/native receipts only; no desktop or original SQLite"})
	flush()

func record(kind: String, fields: Dictionary = {}) -> void:
	if not enabled() or _limit_reached:
		return
	_buffer.append(JSON.stringify({
		"schema":"SG-FP3D-PHYSICAL-TRACE-01",
		"event":kind,
		"monotonic_ms":Time.get_ticks_msec(),
		"unix_time":Time.get_unix_time_from_system(),
		"fields":fields
	}))
	if _buffer.size() >= BUFFER_LIMIT:
		flush()

func pacing(timing: Dictionary, native: Dictionary) -> void:
	if not enabled():
		return
	var now := Time.get_ticks_msec()
	if now - _last_pacing_ms < PACING_INTERVAL_MS:
		return
	_last_pacing_ms = now
	# Godot MAIN LOOP frame spacing, not GPU timestamp or native simulation time.
	record("rolling_frame_pacing", {"window":timing, "native":native})

func flush() -> void:
	if not enabled() or _buffer.is_empty() or _limit_reached:
		return
	var f := FileAccess.open(_file_path, FileAccess.READ_WRITE)
	if f == null:
		return
	f.seek_end()
	for line in _buffer:
		var size: int = line.to_utf8_buffer().size() + 1
		if _bytes_written + size > FILE_LIMIT_BYTES:
			_limit_reached = true
			break
		f.store_line(line)
		_bytes_written += size
	f.close()
	_buffer.clear()

func finish() -> void:
	record("session_end")
	flush()
