extends RefCounted
## PLAY-STATION-02: optional explicit fixture, original C++ station person.
## This bridge never sets person/ship location in SQLite from Godot.
signal person_received(person: Dictionary)
signal status_changed(message: String)
signal connection_changed(connected: bool)

var _pid := -1
var _stdout: FileAccess
var _stderr: FileAccess
var _active := false
var _session := 0
var _generation := -1
var _tick := -1
var _next_request := 1
var _pending := 0
var _last_send := -10000
var _buffer := PackedByteArray()
var _person := {}

func is_active() -> bool:
	return _active

func _status(message: String) -> void:
	status_changed.emit(message)

func _host_path() -> String:
	var binary := "galaxy_desktop_station_host.exe" if OS.get_name() == "Windows" else "galaxy_desktop_station_host"
	if OS.has_feature("editor"):
		return ProjectSettings.globalize_path("res://native/" + binary)
	return OS.get_executable_path().get_base_dir().path_join("native").path_join(binary)

func start() -> void:
	if _active:
		return
	var exe := _host_path()
	if not FileAccess.file_exists(exe):
		_status("STATION native host missing: " + exe)
		return
	# Same original game world as the native in-ship cockpit host, but never
	# two writers at once. The host creates fixtures only for a NEW save.
	var save := OS.get_user_data_dir().path_join("single_galaxy_station_test.sqlite")
	var result := OS.execute_with_pipe(exe, PackedStringArray([save, "--explicit-test-station"]), false)
	if result.is_empty() or not result.has("stdio") or not result.has("pid"):
		_status("Native station host launch failed")
		return
	_stdout = result["stdio"]
	_stderr = result.get("stderr")
	_pid = int(result["pid"])
	_active = true
	_session = 0
	_generation = -1
	_tick = -1
	_pending = 0
	_next_request = 1
	_last_send = -10000
	_buffer.clear()
	_person.clear()
	connection_changed.emit(true)
	_status("Native station: waiting for original person state")

func poll() -> void:
	if not _active:
		return
	for i in range(16):
		var data := _stdout.get_buffer(4096)
		if data.is_empty():
			break
		for b in data:
			if b == 10:
				var message := _buffer.get_string_from_ascii()
				_buffer.clear()
				_accept_line(message)
			elif b == 13:
				continue
			elif b < 32 or b > 126 or _buffer.size() >= 256:
				stop("Malformed native station response")
				return
			else:
				_buffer.append(b)
		if not _active:
			return
	if _pending != 0 and Time.get_ticks_msec() - _last_send > 2500:
		stop("Station response timed out; reconnect to verify original state")
		return
	if not OS.is_process_running(_pid):
		stop("Native station host exited; saved player location retained")

func _integer(value: String) -> bool:
	return not value.is_empty() and value.length() <= 19 and value.is_valid_int()

func _accept_line(line: String) -> void:
	var f := line.split("|", false)
	if f.size() == 4 and f[0] == "DENY" and f[1] == "2":
		if not _integer(f[2]):
			stop("Malformed native station denial")
			return
		var req := int(f[2])
		if req == _pending:
			_pending = 0
		_status("Station action #%d refused: %s" % [req, f[3]])
		return
	if f.size() != 13 or f[0] != "PERSON" or f[1] != "2":
		stop("Invalid station-person native frame")
		return
	for i in range(2, 13):
		if not _integer(f[i]):
			stop("Non-numeric native station field")
			return
	var session := int(f[2])
	var req := int(f[3])
	var tick := int(f[4])
	var gen := int(f[5])
	var state := int(f[6])
	var station := int(f[7])
	var ship := int(f[8])
	var x := int(f[9])
	var z := int(f[10])
	var eligible := int(f[11])
	var receipt := int(f[12])
	if session <= 0 or gen <= 0 or tick < 0 or eligible < 0 or receipt < 0 or \
		((state == 4) and (station <= 0 or ship != 0 or abs(x) > 8000 or abs(z) > 8000)) or \
		((state == 0) and (ship <= 0 or x != 0 or z != 0)) or \
		(state != 4 and state != 0):
		stop("Native station person has impossible location")
		return
	if req == 0:
		if _session != 0 or _generation != -1:
			stop("Repeated station genesis frame")
			return
		_session = session
	else:
		if session != _session or req != _pending or tick <= _tick or gen <= _generation:
			stop("Station acknowledgement is stale or unrequested")
			return
		_pending = 0
	_generation = gen
	_tick = tick
	_person = {"state":state,"station":station,"ship":ship,"x_mm":x,"z_mm":z,
		"eligible_ship":eligible,"receipt":receipt,"generation":gen,"tick":tick}
	person_received.emit(_person.duplicate(true))
	_status("AT STATION" if state == 4 else "ABOARD OWN SHIP")

func _send(command: String) -> void:
	if not _stdout.store_line(command):
		stop("Native station command pipe write failed")
		return
	_stdout.flush()

func submit_walk(heading: Vector3) -> void:
	if not _active or _session <= 0 or _pending != 0 or _person.get("state", -1) != 4:
		return
	if heading.length_squared() < 0.01:
		return
	var now := Time.get_ticks_msec()
	if now - _last_send < 110:
		return
	var d := Vector2(heading.x, heading.z).normalized() * 90.0
	var dx := int(round(d.x))
	var dz := int(round(d.y))
	if dx == 0 and dz == 0:
		return
	var req := _next_request
	_next_request += 1
	_pending = req
	_last_send = now
	_send("WALK|%d|%d|%d|%d|%d" % [_session, req, _generation, dx, dz])

func interact_hangar() -> void:
	if not _active or _session <= 0 or _pending != 0:
		return
	var state := int(_person.get("state", -1))
	var verb := ""
	var target := 0
	if state == 4:
		target = int(_person.get("eligible_ship", 0))
		if target <= 0:
			_status("NO AVAILABLE SHIP: build or recover an existing vessel")
			return
		verb = "BOARD"
	elif state == 0:
		target = int(_person.get("station", 0))
		if target <= 0:
			_status("Cannot disembark in space; dock at a real station first")
			return
		verb = "EXIT"
	else:
		return
	var req := _next_request
	_next_request += 1
	_pending = req
	_last_send = Time.get_ticks_msec()
	_send("%s|%d|%d|%d|%d" % [verb, _session, req, _generation, target])

func stop(message: String = "Station session closed") -> void:
	if _active and _pid > 0 and OS.is_process_running(_pid):
		OS.kill(_pid)
	_pid = -1
	_active = false
	_stdout = null
	_stderr = null
	_pending = 0
	connection_changed.emit(false)
	_status(message)
