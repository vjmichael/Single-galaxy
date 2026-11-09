extends RefCounted
const SGDX_LAUNCH := preload("res://world/sgdx_process_launch.gd")
## PLAY-STATION-02: optional explicit fixture, original C++ station person.
## This bridge never sets person/ship location in SQLite from Godot.
signal person_received(person: Dictionary)
signal refinery_received(snapshot: Dictionary)
signal status_changed(message: String)
signal connection_changed(connected: bool)

var _pid := -1
var _last_shutdown_pid := -1
var _last_shutdown_msec := -10000
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
var _refinery_receiving := false
var _refinery_header := {}
var _refinery_lots: Array[Dictionary] = []
var _refinery_minerals := {}

func is_active() -> bool:
	return _active

func process_id() -> int:
	# Track even a just-stopped writer. A rapid H -> P or P -> H must
	# not race its lingering SQLite WAL lock after an explicit stop.
	if _active:
		return _pid
	if _last_shutdown_pid > 0 and Time.get_ticks_msec() - _last_shutdown_msec < 3000:
		return _last_shutdown_pid
	return -1

func _status(message: String) -> void:
	status_changed.emit(message)

func _host_path() -> String:
	var binary := "galaxy_desktop_station_host.exe" if OS.get_name() == "Windows" else "galaxy_desktop_station_host"
	return SGDX_LAUNCH.native_host_path(binary)

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
	var spec := SGDX_LAUNCH.supervised_command(save, "godot_station", exe,
		PackedStringArray([save, "--explicit-test-station"]))
	if spec.is_empty():
		_status("SGDX supervisor missing or invalid save path; refusing unsupervised station writer")
		return
	var result := OS.execute_with_pipe(spec["path"], spec["args"], false)
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
	_refinery_receiving = false
	_refinery_header.clear()
	_refinery_lots.clear()
	_refinery_minerals.clear()
	connection_changed.emit(true)
	_status("SGDX-SUPERVISED station: waiting for original person state")

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
	if f.size() > 0 and f[0] in ["REFHEAD", "REFLOT", "REFMIN", "REFEND"]:
		_accept_refinery_line(f)
		return
	if f.size() == 4 and f[0] == "DENY" and f[1] == "2":
		if not _integer(f[2]):
			stop("Malformed native station denial")
			return
		var req := int(f[2])
		if req == _pending:
			_pending = 0
			_refinery_receiving = false
			_refinery_header.clear()
			_refinery_lots.clear()
			_refinery_minerals.clear()
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

func _accept_refinery_line(f: PackedStringArray) -> void:
	var kind := f[0]
	if kind == "REFHEAD":
		if f.size() != 18 or _refinery_receiving or _pending <= 0:
			stop("Unexpected refinery snapshot header")
			return
		for i in range(2, 18):
			if not _integer(f[i]):
				stop("Non-numeric original refinery header")
				return
		var session := int(f[2])
		var request := int(f[3])
		var tick := int(f[4])
		var generation := int(f[5])
		var station := int(f[6])
		var shown := int(f[15])
		var all_lots := int(f[16])
		var minerals := int(f[17])
		if session != _session or request != _pending or tick < _tick or generation < _generation or station <= 0 or station != int(_person.get("station", 0)) or int(_person.get("state", -1)) != 4 or shown < 0 or shown > 48 or all_lots < shown or minerals < 0 or minerals > 64 or int(f[7]) not in [0, 1] or int(f[10]) not in [0, 1] or int(f[13]) < 0 or int(f[14]) < 0:
			stop("Original refinery snapshot contradicts station custody")
			return
		_refinery_header = {"station":station, "tick":tick, "generation":generation,
			"installed":int(f[7]), "source_ship":int(f[8]),
			"workers":int(f[9]), "powered":int(f[10]), "energy":int(f[11]),
			"ready_tick":int(f[12]), "ore_tonnes":int(f[13]),
			"mineral_total_kg":int(f[14]), "shown_lots":shown,
			"all_lots":all_lots, "mineral_count":minerals}
		_refinery_lots.clear()
		_refinery_minerals.clear()
		_refinery_receiving = true
		return
	if not _refinery_receiving or f.size() < 4 or not _integer(f[2]) or not _integer(f[3]) or int(f[2]) != _session or int(f[3]) != _pending:
		stop("Unexpected refinery source frame")
		return
	if kind == "REFLOT":
		if f.size() != 9 or _refinery_lots.size() >= int(_refinery_header["shown_lots"]):
			stop("Refinery lot count exceeded source header")
			return
		for i in range(4, 9):
			if not _integer(f[i]):
				stop("Non-numeric refinery lot")
				return
		var lot_id := int(f[4])
		if lot_id <= 0 or int(f[5]) <= 0 or int(f[6]) not in [0, 1, 2] or int(f[7]) <= 0 or int(f[8]) <= 0:
			stop("Refinery lot has invalid physical provenance")
			return
		for lot in _refinery_lots:
			if int(lot["id"]) == lot_id:
				stop("Duplicate refinery lot in native snapshot")
				return
		_refinery_lots.append({"id":lot_id,"rock":int(f[5]),"taxonomy":int(f[6]),
			"tonnes":int(f[7]),"unload_receipt":int(f[8])})
		return
	if kind == "REFMIN":
		if f.size() != 6 or _refinery_minerals.size() >= int(_refinery_header["mineral_count"]) or not _integer(f[5]):
			stop("Invalid refinery product frame")
			return
		var name := f[4]
		if name.length() > 40 or not name.is_valid_identifier() or name.to_lower() != name or int(f[5]) <= 0 or _refinery_minerals.has(name):
			stop("Refinery mineral product name or stock invalid")
			return
		_refinery_minerals[name] = int(f[5])
		return
	if kind == "REFEND":
		if f.size() != 10:
			stop("Invalid refinery receipt length")
			return
		for i in range(4, 10):
			if not _integer(f[i]):
				stop("Non-numeric original refinery receipt")
				return
		var result := int(f[8])
		var transferred := int(f[9])
		var total := 0
		for amount in _refinery_minerals.values():
			total += int(amount)
		if int(f[4]) != int(_refinery_header["tick"]) or int(f[5]) != int(_refinery_header["generation"]) or int(f[6]) != _refinery_lots.size() or int(f[7]) != _refinery_minerals.size() or total != int(_refinery_header["mineral_total_kg"]) or result not in [0, 1, 2] or (result == 1 and transferred != 1000) or (result != 1 and transferred != 0):
			stop("Refinery snapshot and original commit receipt mismatch")
			return
		_refinery_receiving = false
		_generation = int(f[5])
		_tick = int(f[4])
		_pending = 0
		var state := _refinery_header.duplicate(true)
		state["lots"] = _refinery_lots.duplicate(true)
		state["minerals"] = _refinery_minerals.duplicate(true)
		state["result"] = result
		state["transferred_kg"] = transferred
		_refinery_header.clear()
		_refinery_lots.clear()
		_refinery_minerals.clear()
		refinery_received.emit(state)
		_status("Refined 1 tonne into station minerals" if result == 1 else "Station refinery snapshot received" if result == 0 else "Original station refinery command refused")
		return
	stop("Unknown refinery source frame")

func request_refinery_view() -> void:
	if not _active or _session <= 0 or _pending != 0 or int(_person.get("state", -1)) != 4:
		_status("Refinery terminal requires an actual station resident")
		return
	var request := _next_request
	_next_request += 1
	_pending = request
	_last_send = Time.get_ticks_msec()
	_send("REFVIEW|%d|%d|%d|%d" % [_session,request,_generation,int(_person["station"])])

func submit_refining(lot_id: int) -> void:
	if not _active or _session <= 0 or _pending != 0 or int(_person.get("state", -1)) != 4 or lot_id <= 0:
		_status("Refining request requires a selected original ore lot at the station")
		return
	var request := _next_request
	_next_request += 1
	_pending = request
	_last_send = Time.get_ticks_msec()
	_send("REFINE|%d|%d|%d|%d" % [_session,request,_generation,lot_id])

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

func stop(message: String = "Station session closed", graceful: bool = false) -> void:
	if _pid > 0:
		_last_shutdown_pid = _pid
		_last_shutdown_msec = Time.get_ticks_msec()
	if _active and _pid > 0 and OS.is_process_running(_pid):
		if graceful and _stdout != null:
			# The original host exits only after explicit QUIT. SGDX then
			# records a clean exit and releases the writer/Job Object.
			_stdout.store_line("QUIT")
			_stdout.flush()
		else:
			# Malformed protocol, untrusted response or timeout: fail closed.
			# Killing this supervisor kills the native process custody tree.
			OS.kill(_pid)
	_pid = -1
	_active = false
	_stdout = null
	_stderr = null
	_pending = 0
	connection_changed.emit(false)
	_status(message)
