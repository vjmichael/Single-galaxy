extends RefCounted
const SGDX_LAUNCH := preload("res://world/sgdx_process_launch.gd")
## PLAY-STATION-02: optional explicit fixture, original C++ station person.
## This bridge never sets person/ship location in SQLite from Godot.
signal person_received(person: Dictionary)
signal refinery_received(snapshot: Dictionary)
signal colony_population_received(snapshot: Dictionary)
signal dock_received(snapshot: Dictionary)
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
# One user E press can arrive before a pending authoritative WALK receipt.
# Keep it for that one acknowledgement only; NEVER enqueue a second writer,
# replay a rejected action or issue a BOARD from local visual proximity.
var _pending_walk := 0
var _queued_hangar_intent := false
var _queued_hangar_ready_msec := 0
# Native host rejects a command <45ms after the prior accepted command.
# Conservative 130ms spacing avoids asynchronous WALK->BOARD rate denials.
const NATIVE_HANGAR_COMMAND_GAP_MS := 130
var _last_send := -10000
var _buffer := PackedByteArray()
var _person := {}
var _refinery_receiving := false
var _pending_refinery_kind := "" # VIEW, REFINE or UNLOAD; never reuse a stale receipt
var _pending_population := false
var _pending_dock_kind := ""
var _dock_initial_read_done := false
var _dock_auto_remaining := 0
var _dock_auto_staff := 0
var _last_dock_automation_ms := -10000
const DOCK_AUTO_INTERVAL_MS := 900
var _refinery_header := {}
var _refinery_lots: Array[Dictionary] = []
var _refinery_minerals := {}
var _last_startup_diagnostic := ""

func _write_station_diagnostic(reason: String, detail: String) -> void:
	# No SQLite, private minidumps, memory dumps or captured desktop content.
	# Exactly one bounded local failure summary is overwritten on each error,
	# with an optional copy beside the user's explicit playtest evidence ZIP.
	var report := JSON.stringify({"schema":"SG-FP3D-START-01",
		"unix_time":Time.get_unix_time_from_system(),
		"reason":reason.substr(0, 320),
		"stderr":detail.substr(0, 2048),
		"native_person_admitted":_session > 0})
	var destinations := PackedStringArray(["user://sg_station_last_failure.json"])
	var capture_dir: String = OS.get_environment("SG_FP3D_OPERATOR_CAPTURE_DIR")
	if not capture_dir.is_empty() and capture_dir.is_absolute_path() and DirAccess.dir_exists_absolute(capture_dir):
		destinations.append(capture_dir.path_join("sg_station_last_failure.json"))
	for destination in destinations:
		var f := FileAccess.open(destination, FileAccess.WRITE)
		if f != null:
			f.store_string(report + "\n")
			f.close()

func _unexpected_native_exit() -> String:
	var native_error := ""
	# This is read only AFTER the supervised child has exited. Never block
	# the active game loop by draining a still-running process's stderr.
	if _stderr != null:
		native_error = _stderr.get_buffer(4096).get_string_from_utf8().strip_edges()
	var reason := "Original station C++/SGDX child exited unexpectedly"
	_write_station_diagnostic(reason, native_error)
	_last_startup_diagnostic = native_error.substr(0, 300)
	if native_error.is_empty():
		return reason + "; your world is unchanged. H retries safely; see sg_station_last_failure.json"
	return reason + ": " + native_error.replace("\r", " ").replace("\n", " ").substr(0, 190) + " (H retry; saved world unchanged)"

func is_active() -> bool:
	return _active

func process_id() -> int:
	# Track even a just-stopped writer. A rapid H -> P or P -> H must
	# not race its lingering SQLite WAL lock after an explicit stop.
	if _active:
		return _pid
	# Never forget a still-running guardian merely because three seconds passed.
	# A later same-role restart must not bypass the SQLite writer handoff.
	if _last_shutdown_pid > 0:
		if OS.is_process_running(_last_shutdown_pid):
			return _last_shutdown_pid
		_last_shutdown_pid = -1
	return -1

func _status(message: String) -> void:
	status_changed.emit(message)

func _host_path() -> String:
	var binary := "galaxy_desktop_station_host.exe" if OS.get_name() == "Windows" else "galaxy_desktop_station_host"
	return SGDX_LAUNCH.native_host_path(binary)

func start() -> void:
	if _active:
		return
	# A previous QUIT may be delayed by a blocked host or SQLite flush.
	# Do not start a second writer for this role while its SGDX custodian lives.
	if _last_shutdown_pid > 0:
		if OS.is_process_running(_last_shutdown_pid):
			_status("Previous SGDX writer still alive; refusing overlapping native session")
			return
		_last_shutdown_pid = -1
	var exe := _host_path()
	if not FileAccess.file_exists(exe):
		_status("STATION native host missing: " + exe)
		return
	# Same original game world as the native in-ship cockpit host, but never
	# two writers at once. The host creates fixtures only for a NEW save.
	# A fresh Windows Godot profile may not yet have its app_userdata
	# directory. Create ONLY the directory, never replace any SQLite world.
	var save_dir: String = OS.get_user_data_dir()
	if not DirAccess.dir_exists_absolute(save_dir):
		if DirAccess.make_dir_recursive_absolute(save_dir) != OK:
			_status("Saved colony directory is not writable; refusing to reset or substitute SQLite world")
			return
	var save := save_dir.path_join("single_galaxy_station_test.sqlite")
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
	_pending_walk = 0
	_queued_hangar_intent = false
	_queued_hangar_ready_msec = 0
	_pending_refinery_kind = ""
	_pending_population = false
	_pending_dock_kind = ""
	_dock_initial_read_done = false
	_dock_auto_remaining = 0
	_dock_auto_staff = 0
	_last_dock_automation_ms = -10000
	_next_request = 1
	_last_send = -10000
	_buffer.clear()
	_person.clear()
	_last_startup_diagnostic = ""
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
		var failure := _unexpected_native_exit()
		stop(failure)
		return
	_dispatch_queued_hangar_if_ready()
	_automatic_dock_service()

func _integer(value: String) -> bool:
	return not value.is_empty() and value.length() <= 19 and value.is_valid_int()

func _accept_line(line: String) -> void:
	var f := line.split("|", false)
	if f.size() > 0 and f[0] == "DOCK":
		_accept_dock(f)
		return
	if f.size() > 0 and f[0] == "COLPOP":
		_accept_colony_population(f)
		return
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
			if req == _pending_walk:
				_pending_walk = 0
				_queued_hangar_intent = false
				_queued_hangar_ready_msec = 0
				_status("Native WALK rejected; press E again after reaching the hatch")
			_pending_refinery_kind = ""
			_pending_population = false
			_pending_dock_kind = ""
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
	if state != 4:
		_dock_initial_read_done = false
		_dock_auto_remaining = 0
		_dock_auto_staff = 0
	var last_walk_confirmed: bool = req > 0 and req == _pending_walk
	if last_walk_confirmed:
		_pending_walk = 0
	_generation = gen
	_tick = tick
	_person = {"state":state,"station":station,"ship":ship,"x_mm":x,"z_mm":z,
		"eligible_ship":eligible,"receipt":receipt,"generation":gen,"tick":tick}
	person_received.emit(_person.duplicate(true))
	_status("AT STATION" if state == 4 else "ABOARD OWN SHIP")
	if last_walk_confirmed and _queued_hangar_intent:
		# Fresh PERSON does not waive original C++'s per-command rate gate.
		# Wait before dispatching ONE explicit E, with current generation.
		_queued_hangar_ready_msec = maxi(Time.get_ticks_msec() + 75,
			_last_send + NATIVE_HANGAR_COMMAND_GAP_MS)
		_status("E received — synchronizing original station boarding after WALK receipt")

func _accept_dock(f: PackedStringArray) -> void:
	# Every status is a response to one local one-use command. Never allow
	# client forecasts to replace authoritative worker or cargo counts.
	if f.size() != 17 or f[1] != "1" or _pending_dock_kind.is_empty():
		stop("Unrequested dock service receipt")
		return
	for i in range(2, 15):
		if not _integer(f[i]):
			stop("Malformed original dock receipt")
			return
	var session := int(f[2])
	var req := int(f[3])
	var tick := int(f[4])
	var generation := int(f[5])
	var station := int(f[6])
	var staff := int(f[7])
	var passengers := int(f[8])
	var tonnes := int(f[9])
	var jobs := int(f[10])
	var held := int(f[11])
	var completed_people := int(f[12])
	var completed_tonnes := int(f[13])
	var result := int(f[14])
	var expected_result := {"VIEW":[0],"ASSIGN":[1],"RELEASE":[2],
		"PASS":[3],"FREIGHT":[4],"WORK":[5,6]}
	if session != _session or req != _pending or tick < _tick or generation < _generation or \
			int(_person.get("state",-1)) != 4 or station != int(_person.get("station",-1)) or \
			staff < 0 or staff > 16 or passengers < 0 or passengers > 128 or \
			tonnes < 0 or tonnes > 8192 or jobs < 0 or jobs > 128 or held < 0 or held > 128 or \
			completed_people < 0 or completed_tonnes < 0 or \
			not expected_result.get(_pending_dock_kind,[]).has(result):
		stop("Original dock receipt violates source custody or queue bounds")
		return
	_pending = 0
	_pending_dock_kind = ""
	_dock_initial_read_done = true
	_dock_auto_remaining = passengers + tonnes
	_dock_auto_staff = staff
	_tick = tick
	_generation = generation
	var snapshot := {"station":station,"dock_staff":staff,
		"passengers":passengers,"freight_tonnes":tonnes,"freight_jobs":jobs,
		"held_jobs":held,"checked_in":completed_people,
		"handled_tonnes":completed_tonnes,"result":result,
		"tick":tick,"generation":generation}
	dock_received.emit(snapshot)
	_status("Dock assignment changed" if result in [1,2] else
		"Freight service updated" if result in [4,5] else
		"Passenger checked into local queue" if result==3 else
		"Dock workers unavailable or queue idle" if result==6 else
		"Dock services ready")

func _automatic_dock_service() -> void:
	# A bounded foreground worker scheduler, not a detached SQLite writer:
	# the same active native station host commits all original-world actions.
	# No scheduling while in flight, during boarding, during user commands,
	# or after disconnect. No speculative NPC/cargo adjustment in Godot.
	if not _active or _session<=0 or _pending!=0 or _queued_hangar_intent or \
			int(_person.get("state",-1)) != 4:
		return
	var now := Time.get_ticks_msec()
	if now-_last_dock_automation_ms<DOCK_AUTO_INTERVAL_MS or \
			now-_last_send<NATIVE_HANGAR_COMMAND_GAP_MS:
		return
	_last_dock_automation_ms = now
	if not _dock_initial_read_done:
		request_dock_status()
	elif _dock_auto_remaining>0 and _dock_auto_staff>0:
		advance_dock_services()

func request_dock_status() -> bool:
	return _submit_dock("VIEW",0)

func assign_available_dock_worker() -> bool:
	return _submit_dock("ASSIGN",0)

func end_dock_worker_shift() -> bool:
	return _submit_dock("RELEASE",0)

func register_local_passenger_checkin() -> bool:
	return _submit_dock("PASS",0)

func queue_docked_freight_tonne() -> bool:
	if int(_person.get("eligible_ship",0)) <= 0:
		_status("No original owned ship docked at this colony")
		return false
	return _submit_dock("FREIGHT",int(_person["eligible_ship"]),1)

func advance_dock_services() -> bool:
	return _submit_dock("WORK",int(_person.get("station",0)))

func _submit_dock(kind: String, source: int, quantity: int = 0) -> bool:
	if not _active or _session<=0 or _pending!=0 or \
			int(_person.get("state",-1)) != 4 or \
			Time.get_ticks_msec()-_last_send < NATIVE_HANGAR_COMMAND_GAP_MS:
		_status("Dock service busy; retry after current native acknowledgement")
		return false
	var op := {"VIEW":"DOCKVIEW","ASSIGN":"DOCKASSIGN",
		"RELEASE":"DOCKRELEASE","PASS":"DOCKPASS","FREIGHT":"DOCKFREIGHT",
		"WORK":"DOCKWORK"}.get(kind,"")
	if op.is_empty():
		return false
	var request := _next_request
	_next_request += 1
	_pending = request
	_pending_dock_kind = kind
	_last_send = Time.get_ticks_msec()
	var line := "%s|%d|%d|%d|%d" % [op,_session,request,_generation,source]
	if kind=="FREIGHT":
		line += "|%d" % quantity
	_send(line)
	return true

func _accept_colony_population(f: PackedStringArray) -> void:
	# A single read-only observation from bounded named NPC source records.
	# Never materialize individual NPCs or a second population ledger.
	if f.size()!=10 or f[1]!="1" or not _pending_population:
		stop("Unrequested colony population observation")
		return
	for i in range(2,10):
		if not _integer(f[i]):
			stop("Malformed colony population identity")
			return
	var session := int(f[2])
	var request := int(f[3])
	var tick := int(f[4])
	var generation := int(f[5])
	var station := int(f[6])
	var present := int(f[7])
	var assigned := int(f[8])
	var truncated := int(f[9])
	if session!=_session or request!=_pending or tick!=_tick or \
			generation!=_generation or int(_person.get("state",-1))!=4 or \
			station!=int(_person.get("station",-1)) or \
			present<0 or present>4096 or assigned<0 or assigned>16 or \
			truncated not in [0,1] or (assigned>present and truncated==0):
		stop("Colony population from mismatched source or unbounded count")
		return
	_pending = 0
	_pending_population = false
	colony_population_received.emit({"station":station,"source_count":present,
		"dock_staff":assigned,"scan_truncated":truncated==1,
		"tick":tick,"generation":generation})

func request_colony_population() -> bool:
	if not _active or _session<=0 or _pending!=0 or \
			int(_person.get("state",-1))!=4 or \
			Time.get_ticks_msec()-_last_send<130:
		return false
	var request := _next_request
	_next_request += 1
	_pending = request
	_pending_population = true
	_last_send = Time.get_ticks_msec()
	_send("POPVIEW|%d|%d|%d|%d" % [
		_session,request,_generation,int(_person["station"])])
	return true

func _accept_refinery_line(f: PackedStringArray) -> void:
	var kind := f[0]
	if kind == "REFHEAD":
		if f.size() != 18 or _refinery_receiving or _pending <= 0 or _pending_refinery_kind.is_empty():
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
			"all_lots":all_lots, "mineral_count":minerals,
			"eligible_ship":int(_person.get("eligible_ship", 0))}
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
		if int(f[4]) != int(_refinery_header["tick"]) or int(f[5]) != int(_refinery_header["generation"]) or int(f[6]) != _refinery_lots.size() or int(f[7]) != _refinery_minerals.size() or total != int(_refinery_header["mineral_total_kg"]) or result not in [0, 1, 2, 3, 4] or (result == 1 and transferred != 1000) or (result == 3 and transferred != 1) or (result not in [1, 3] and transferred != 0) or (_pending_refinery_kind == "VIEW" and result != 0) or (_pending_refinery_kind == "REFINE" and result not in [1, 2]) or (_pending_refinery_kind == "UNLOAD" and result not in [3, 4]):
			stop("Refinery snapshot and original commit receipt mismatch")
			return
		_refinery_receiving = false
		_pending_refinery_kind = ""
		_generation = int(f[5])
		_tick = int(f[4])
		_pending = 0
		var state := _refinery_header.duplicate(true)
		state["lots"] = _refinery_lots.duplicate(true)
		state["minerals"] = _refinery_minerals.duplicate(true)
		state["result"] = result
		state["transferred_kg"] = transferred if result == 1 else 0
		state["unloaded_tonnes"] = transferred if result == 3 else 0
		_refinery_header.clear()
		_refinery_lots.clear()
		_refinery_minerals.clear()
		refinery_received.emit(state)
		_status("Refined 1 tonne into station minerals" if result == 1 else "Unloaded 1 original tonne into the station hangar" if result == 3 else "Station refinery snapshot received" if result == 0 else "Original station cargo/refinery command refused")
		return
	stop("Unknown refinery source frame")

func request_colony_inventory() -> void:
	# Colony-local operations panel: user must be physically at THIS native
	# station, but need not walk to a separate refinery building.
	if not _active or _session <= 0 or _pending != 0 or \
			int(_person.get("state", -1)) != 4 or int(_person.get("station",0)) <= 0:
		_status("Colony inventory requires residence at this original station")
		return
	var request := _next_request
	_next_request += 1
	_pending = request
	_pending_refinery_kind = "VIEW"
	_last_send = Time.get_ticks_msec()
	_send("COLVIEW|%d|%d|%d|%d" % [
		_session,request,_generation,int(_person["station"])])

func request_refinery_view() -> void:
	if not _active or _session <= 0 or _pending != 0 or int(_person.get("state", -1)) != 4:
		_status("Refinery terminal requires an actual station resident")
		return
	var request := _next_request
	_next_request += 1
	_pending = request
	_pending_refinery_kind = "VIEW"
	_last_send = Time.get_ticks_msec()
	_send("REFVIEW|%d|%d|%d|%d" % [_session,request,_generation,int(_person["station"])])

func submit_colony_refining(lot_id: int) -> void:
	# No remote intercolony inventory transfer. The actual original C++
	# refinery must be built, crewed and powered at the current colony.
	if not _active or _session <= 0 or _pending != 0 or \
			int(_person.get("state",-1)) != 4 or lot_id <= 0:
		_status("Colony refining requires a resident and selected local lot")
		return
	var request := _next_request
	_next_request += 1
	_pending = request
	_pending_refinery_kind = "REFINE"
	_last_send = Time.get_ticks_msec()
	_send("COLREFINE|%d|%d|%d|%d" % [_session,request,_generation,lot_id])

func submit_refining(lot_id: int) -> void:
	if not _active or _session <= 0 or _pending != 0 or int(_person.get("state", -1)) != 4 or lot_id <= 0:
		_status("Refining request requires a selected original ore lot at the station")
		return
	var request := _next_request
	_next_request += 1
	_pending = request
	_pending_refinery_kind = "REFINE"
	_last_send = Time.get_ticks_msec()
	_send("REFINE|%d|%d|%d|%d" % [_session,request,_generation,lot_id])

func unload_colony_docked_ship_tonne() -> void:
	# Only the latest trusted original PERSON can pick the matching docked
	# owned ship. A UI button does not remotely move cargo across colonies.
	if not _active or _session <= 0 or _pending != 0 or \
			int(_person.get("state",-1)) != 4:
		_status("Colony logistics requires a resident and idle native writer")
		return
	var ship_id := int(_person.get("eligible_ship",0))
	if ship_id <= 0:
		_status("No physically docked owned ship at this colony")
		return
	var request := _next_request
	_next_request += 1
	_pending = request
	_pending_refinery_kind = "UNLOAD"
	_last_send = Time.get_ticks_msec()
	_send("COLUNLOAD|%d|%d|%d|%d" % [_session,request,_generation,ship_id])

func unload_owned_ship_tonne() -> void:
	# The ship ID originates in the latest authenticated PERSON receipt.
	# It is a selector only: native C++ independently checks station, ownership,
	# docking, cargo, terminal distance and the one-tonne journaled command.
	if not _active or _session <= 0 or _pending != 0 or int(_person.get("state", -1)) != 4:
		_status("Unloading requires a real station resident and an idle original host")
		return
	var ship_id := int(_person.get("eligible_ship", 0))
	if ship_id <= 0:
		_status("No eligible existing owned docked ship to unload")
		return
	var request := _next_request
	_next_request += 1
	_pending = request
	_pending_refinery_kind = "UNLOAD"
	_last_send = Time.get_ticks_msec()
	_send("UNLOAD|%d|%d|%d|%d" % [_session,request,_generation,ship_id])

func _send(command: String) -> void:
	if not _stdout.store_line(command):
		stop("Native station command pipe write failed")
		return
	_stdout.flush()

func submit_walk(heading: Vector3, running: bool = false) -> void:
	if not _active or _session <= 0 or _pending != 0 or _queued_hangar_intent or _person.get("state", -1) != 4:
		return
	if heading.length_squared() < 0.01:
		return
	var now := Time.get_ticks_msec()
	# Same 100mm legal original step; walk/sprint differ only in bounded
	# input cadence. C++ retains its 45ms host limit and position checks.
	if now - _last_send < (70 if running else 120):
		return
	var d := Vector2(heading.x, heading.z).normalized() * 100.0
	var dx := int(round(d.x))
	var dz := int(round(d.y))
	if dx == 0 and dz == 0:
		return
	var req := _next_request
	_next_request += 1
	_pending = req
	_pending_walk = req
	_last_send = now
	_send("WALK|%d|%d|%d|%d|%d" % [_session, req, _generation, dx, dz])

func _dispatch_queued_hangar_if_ready() -> void:
	if not _queued_hangar_intent or not _active or _session <= 0 or _pending != 0 or _queued_hangar_ready_msec <= 0:
		return
	if Time.get_ticks_msec() < _queued_hangar_ready_msec:
		return
	_queued_hangar_intent = false
	_queued_hangar_ready_msec = 0
	# No local boarding authority: the C++ transaction revalidates the
	# current original PERSON, ship custody, distance and docking.
	interact_hangar()

func interact_hangar() -> void:
	if not _active or _session <= 0:
		_status("Original station not connected; use Retry after a real failure")
		return
	if _queued_hangar_intent and _pending == 0:
		_status("One boarding request already queued — waiting for native cooldown")
		return
	if _pending != 0:
		if _pending == _pending_walk and int(_person.get("state", -1)) == 4:
			_queued_hangar_intent = true
			_queued_hangar_ready_msec = 0
			_status("E received — waiting for native walking confirmation before boarding")
		else:
			_status("Native station action pending — retry E after its receipt")
		return
	var state := int(_person.get("state", -1))
	var verb := ""
	var target := 0
	if state == 4:
		target = int(_person.get("eligible_ship", 0))
		if target <= 0:
			_status("NO AVAILABLE SHIP: build or recover an existing vessel")
			return
		# This is a UX preflight only. The original C++ transaction still
		# independently checks exact hatch distance, ownership and docking.
		# Never send an obviously doomed BOARD far from the native hatch.
		var x_mm: int = int(_person.get("x_mm", 2147483647))
		var z_mm: int = int(_person.get("z_mm", 2147483647))
		if absi(x_mm) > 600 or absi(z_mm) > 600:
			_status("Get closer to the CYAN BOARDING HATCH, then press E (native position %.2f, %.2f m)" % [float(x_mm)/1000.0,float(z_mm)/1000.0])
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
	var now: int = Time.get_ticks_msec()
	if now - _last_send < NATIVE_HANGAR_COMMAND_GAP_MS:
		_queued_hangar_intent = true
		_queued_hangar_ready_msec = _last_send + NATIVE_HANGAR_COMMAND_GAP_MS
		_status("E received — waiting for original C++ command cooldown")
		return
	var req := _next_request
	_next_request += 1
	_pending = req
	_last_send = now
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
	_pending_walk = 0
	_queued_hangar_intent = false
	_queued_hangar_ready_msec = 0
	_pending_refinery_kind = ""
	_pending_dock_kind = ""
	_dock_initial_read_done = false
	_dock_auto_remaining = 0
	_dock_auto_staff = 0
	connection_changed.emit(false)
	_status(message)
