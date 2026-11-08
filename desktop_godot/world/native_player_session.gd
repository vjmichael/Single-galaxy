extends RefCounted
## FP3D-02F: local Godot child stdio, bounded MOVE request protocol.
## Only the C++ host commits pose and native STEP; Godot cannot write SQLite.
signal pose_received(pose: Dictionary)
signal status_changed(message: String)
signal connection_changed(connected: bool)

const MAX_LINE := 256
var _pid := -1
var _stdout: FileAccess
var _stderr: FileAccess
var _active := false
var _session := 0
var _sequence := -1
var _generation := -1
var _tick := -1
var _next_request := 1
var _pending := 0
var _last_send_msec := -10000
var _buffer := PackedByteArray()

func is_active() -> bool:
	return _active

func _status(message: String) -> void:
	status_changed.emit(message)
	var f := FileAccess.open("user://sg_native_player_status.log", FileAccess.READ_WRITE)
	if f == null:
		f = FileAccess.open("user://sg_native_player_status.log", FileAccess.WRITE)
	if f != null:
		f.seek_end()
		f.store_line(Time.get_datetime_string_from_system() + " | " + message)
		f.close()

func _host_path() -> String:
	var binary := "galaxy_desktop_player_host.exe" if OS.get_name() == "Windows" else "galaxy_desktop_player_host"
	if OS.has_feature("editor"):
		return ProjectSettings.globalize_path("res://native/" + binary)
	return OS.get_executable_path().get_base_dir().path_join("native").path_join(binary)

func start() -> void:
	if _active:
		return
	var path := _host_path()
	if not FileAccess.file_exists(path):
		_status("NATIVE PLAYER HOST MISSING: " + path)
		return
	var save := OS.get_user_data_dir().path_join("single_galaxy_authoritative.sqlite")
	var result := OS.execute_with_pipe(path, PackedStringArray([save]), false)
	if result.is_empty() or not result.has("stdio") or not result.has("pid"):
		_status("NATIVE PLAYER HOST LAUNCH FAILED")
		return
	_stdout = result["stdio"]
	_stderr = result.get("stderr")
	_pid = result["pid"]
	_active = true
	_session = 0
	_sequence = -1
	_generation = -1
	_tick = -1
	_next_request = 1
	_pending = 0
	_last_send_msec = -10000
	_buffer.clear()
	connection_changed.emit(true)
	_status("F8 NATIVE PLAYER: waiting for verified initial pose; no local walking")

func poll() -> void:
	if not _active:
		return
	for i in range(16):
		var data := _stdout.get_buffer(4096)
		if data.is_empty():
			break
		for b in data:
			if b == 10:
				var line := _buffer.get_string_from_ascii()
				_buffer.clear()
				_accept_line(line)
			elif b == 13:
				continue
			elif b < 32 or b > 126 or _buffer.size() >= MAX_LINE:
				stop("Rejected malformed/oversize native response")
				return
			else:
				_buffer.append(b)
		if not _active:
			return
	if _pending != 0 and Time.get_ticks_msec() - _last_send_msec > 2500:
		stop("Native movement acknowledgement timed out; reconnect to verify saved pose")
		return
	if not OS.is_process_running(_pid):
		stop("Native player exited; last confirmed position retained; see user://sg_native_player_status.log")

func _integer(value: String) -> bool:
	return not value.is_empty() and value.length() <= 19 and value.is_valid_int()

func _accept_line(line: String) -> void:
	var parts := line.split("|", false)
	if parts.size() == 4 and parts[0] == "DENY" and parts[1] == "1":
		if not _integer(parts[2]):
			stop("Malformed native denial receipt")
			return
		var request := int(parts[2])
		if request == _pending:
			_pending = 0
		_status("NATIVE DENIED move #%d: %s; no position advanced" % [request, parts[3]])
		return
	if parts.size() != 11 or parts[1] != "1" or (parts[0] != "POSE" and parts[0] != "ACK"):
		stop("Native player protocol frame invalid")
		return
	for i in range(2, 11):
		if not _integer(parts[i]):
			stop("Native player protocol nonnumeric field")
			return
	var session := int(parts[2])
	var request := int(parts[3])
	var seq := int(parts[4])
	var x := int(parts[5])
	var y := int(parts[6])
	var z := int(parts[7])
	var tick := int(parts[8])
	var generation := int(parts[9])
	var ship := int(parts[10])
	if session <= 0 or ship <= 0 or abs(x) > 8000 or abs(z) > 8000 or y != 0 or seq < 0 or tick < 0 or generation < 1:
		stop("Native pose fields out of bounds")
		return
	if parts[0] == "POSE":
		if _session != 0 or request != 0:
			stop("Duplicate initial native identity")
			return
		_session = session
	else:
		if session != _session or request != _pending or seq != _sequence + 1 or tick <= _tick or generation <= _generation:
			stop("Native move acknowledgement has invalid sequence or identity")
			return
		_pending = 0
	_sequence = seq
	_tick = tick
	_generation = generation
	pose_received.emit({"x_mm":x,"y_mm":y,"z_mm":z,"seq":seq,"tick":tick,"gen":generation,"ship":ship})
	_status("NATIVE AUTHORITATIVE FPS (F8 stop): step %d / tick %d / ship %d" % [seq,tick,ship])

func submit_motion(heading: Vector3) -> void:
	if not _active or _session <= 0 or _pending != 0:
		return
	if heading.length_squared() < 0.001:
		return
	var now := Time.get_ticks_msec()
	if now - _last_send_msec < 60:
		return
	var flat := Vector2(heading.x, heading.z).normalized() * 99.0
	var dx := roundi(flat.x)
	var dz := roundi(flat.y)
	if dx == 0 and dz == 0:
		return
	var req := _next_request
	_next_request += 1
	_pending = req
	_last_send_msec = now
	if not _stdout.store_line("MOVE|%d|%d|%d|%d|%d" % [_session,req,_sequence,dx,dz]):
		stop("Native player command pipe write failed; saved pose will be verified on reconnect")
		return
	_stdout.flush()

func stop(message: String = "Native player stopped; last committed pose retained") -> void:
	if _active and _pid > 0 and OS.is_process_running(_pid):
		OS.kill(_pid)
	_active = false
	_pid = -1
	_stdout = null
	_stderr = null
	_pending = 0
	connection_changed.emit(false)
	_status(message)
