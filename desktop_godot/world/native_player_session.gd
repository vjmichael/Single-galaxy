extends RefCounted
## FP3D-02F: local Godot child stdio, bounded MOVE request protocol.
## Only the C++ host commits pose and native STEP; Godot cannot write SQLite.
signal pose_received(pose: Dictionary)
signal ship_state_received(ship: Dictionary)
signal status_changed(message: String)
signal connection_changed(connected: bool)

const MAX_LINE := 256
var _pid := -1
var _stdout: FileAccess
var _stderr: FileAccess
var _active := false
var _session := 0
var _sequence := -1
var _ship := 0
var _initial_ship_seen := false
var _flight_cycle := 0
var _seat_sequence := -1
var _seated := false
var _pending_kind := ""
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

func start(save_override: String = "") -> void:
	if _active:
		return
	var path := _host_path()
	if not FileAccess.file_exists(path):
		_status("NATIVE PLAYER HOST MISSING: " + path)
		return
	var save := save_override if not save_override.is_empty() else OS.get_user_data_dir().path_join("single_galaxy_authoritative.sqlite")
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
	_ship = 0
	_initial_ship_seen = false
	_flight_cycle = 0
	_seat_sequence = -1
	_seated = false
	_pending_kind = ""
	_generation = -1
	_tick = -1
	_next_request = 1
	_pending = 0
	_last_send_msec = -10000
	_buffer.clear()
	connection_changed.emit(true)
	_status("P NATIVE PLAYER: waiting for verified initial pose; no local walking")

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
		stop("Native command acknowledgement timed out; reconnect to verify saved world")
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
			_pending_kind = ""
		_status("NATIVE DENIED action #%d: %s; original game did not accept it" % [request, parts[3]])
		return
	if parts.size() == 17 and parts[0] == "SHIP":
		_accept_ship(parts)
		return
	if parts.size() != 13 or parts[1] != "1" or (parts[0] != "POSE" and parts[0] != "ACK"):
		stop("Native player protocol frame invalid")
		return
	for i in range(2, 13):
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
	var seated_number := int(parts[11])
	var seat_sequence := int(parts[12])
	if session <= 0 or ship <= 0 or abs(x) > 8000 or abs(z) > 8000 or y != 0 or seq < 0 or tick < 0 or generation < 1 or (seated_number != 0 and seated_number != 1) or seat_sequence < 0:
		stop("Native pose fields out of bounds")
		return
	if parts[0] == "POSE":
		if _session != 0 or request != 0:
			stop("Duplicate initial native identity")
			return
		_session = session
		_ship = ship
	else:
		if session != _session or ship != _ship or request != _pending or tick <= _tick or generation <= _generation:
			stop("Native action acknowledgement has invalid identity or journal generation")
			return
		if _pending_kind == "MOVE":
			if seq != _sequence + 1 or seat_sequence != _seat_sequence or (seated_number == 1) != _seated:
				stop("Native walking acknowledgement mismatches committed movement or seat mode")
				return
		elif _pending_kind == "SEAT":
			if seq != _sequence or seat_sequence != _seat_sequence + 1 or (seated_number == 1) == _seated:
				stop("Native cockpit acknowledgement mismatches committed seat transition")
				return
		else:
			stop("Unrequested native acknowledgement")
			return
		_pending = 0
		_pending_kind = ""
	_sequence = seq
	_seat_sequence = seat_sequence
	_seated = seated_number == 1
	_tick = tick
	_generation = generation
	pose_received.emit({"x_mm":x,"y_mm":y,"z_mm":z,"seq":seq,"tick":tick,"gen":generation,"ship":ship,"seated":_seated,"seat_seq":_seat_sequence})
	_status("NATIVE %s (P stop): walk step %d / seat receipt %d / tick %d" % ["SEATED" if _seated else "ON FOOT",seq,_seat_sequence,tick])

func _accept_ship(parts: PackedStringArray) -> void:
    # Physical ship telemetry comes ONLY from the native host. The initial
    # frame is paired with POSE; subsequent frames need one pending FLIGHT.
    if parts[1] != "1":
        stop("Bad native ship protocol version")
        return
    for i in range(2, 17):
        if not _integer(parts[i]):
            stop("Native ship frame has nonnumeric field")
            return
    var session := int(parts[2])
    var request := int(parts[3])
    var command_id := int(parts[4])
    var outcome := int(parts[5])
    var ship := int(parts[6])
    var tick := int(parts[7])
    var generation := int(parts[8])
    var px := int(parts[9])
    var py := int(parts[10])
    var pz := int(parts[11])
    var vx := int(parts[12])
    var vy := int(parts[13])
    var vz := int(parts[14])
    var heading := int(parts[15])
    var dock := int(parts[16])
    if session != _session or ship != _ship or ship <= 0 or tick < 0 or generation < 1 or \
        outcome < 0 or outcome > 128 or heading < 0 or heading >= 360 or dock < 0 or dock > 3 or \
        abs(vx) > 20000 or abs(vy) > 20000 or abs(vz) > 20000 or \
        abs(px) > 1000000000000000 or abs(py) > 1000000000000000 or abs(pz) > 1000000000000000:
        stop("Native ship identity, physics or bounds invalid")
        return
    if request == 0:
        if _initial_ship_seen or command_id != 0 or tick != _tick or generation != _generation:
            stop("Unexpected initial native ship frame")
            return
        _initial_ship_seen = true
    else:
        if not _initial_ship_seen or _pending_kind != "FLIGHT" or request != _pending or \
            command_id < 1152921504606846976 or tick <= _tick or generation <= _generation:
            stop("Unrequested or stale native flight acknowledgement")
            return
        _pending = 0
        _pending_kind = ""
        _tick = tick
        _generation = generation
    ship_state_received.emit({"ship":ship,"command_id":command_id,"outcome":outcome,
        "tick":tick,"gen":generation,"x_mm":px,"y_mm":py,"z_mm":pz,
        "vx":vx,"vy":vy,"vz":vz,"heading":heading,"dock":dock})
    if request != 0 and outcome != 0:
        _status("Original ship command rejected with outcome code %d (native journal retained)" % outcome)

func submit_flight(actions: PackedStringArray) -> void:
    if not _active or _session <= 0 or not _seated or not _initial_ship_seen or \
        _pending != 0 or actions.is_empty():
        return
    var now := Time.get_ticks_msec()
    if now - _last_send_msec < 110:
        return
    # Rotate through held action candidates, so simultaneous yaw and forward
    # propulsion each reach the native original simulation across ticks.
    var verb := actions[_flight_cycle % actions.size()]
    _flight_cycle += 1
    var req := _next_request
    _next_request += 1
    _pending = req
    _pending_kind = "FLIGHT"
    _last_send_msec = now
    if not _stdout.store_line("FLIGHT|%d|%d|%d|%s" % [_session,req,_seat_sequence,verb]):
        stop("Native flight command pipe write failed; world state must be reverified")
        return
    _stdout.flush()

func submit_motion(heading: Vector3) -> void:
	if not _active or _session <= 0 or _pending != 0 or _seated:
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
	_pending_kind = "MOVE"
	_last_send_msec = now
	if not _stdout.store_line("MOVE|%d|%d|%d|%d|%d" % [_session,req,_sequence,dx,dz]):
		stop("Native player command pipe write failed; saved pose will be verified on reconnect")
		return
	_stdout.flush()

func toggle_seat() -> void:
	# E requests a cockpit transition; only the C++ host may commit the result.
	if not _active or _session <= 0 or _pending != 0:
		return
	var req := _next_request
	_next_request += 1
	_pending = req
	_pending_kind = "SEAT"
	_last_send_msec = Time.get_ticks_msec()
	var desired := 0 if _seated else 1
	if not _stdout.store_line("SEAT|%d|%d|%d|%d" % [_session,req,_seat_sequence,desired]):
		stop("Native cockpit command pipe write failed; prior state retained")
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
	_pending_kind = ""
	connection_changed.emit(false)
	_status(message)
