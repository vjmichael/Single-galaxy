extends RefCounted
const SGDX_LAUNCH := preload("res://world/sgdx_process_launch.gd")
## FP3D-02F: local Godot child stdio, bounded MOVE request protocol.
## Only the C++ host commits pose and native STEP; Godot cannot write SQLite.
signal pose_received(pose: Dictionary)
signal ship_state_received(ship: Dictionary)
signal station_nav_received(nav: Dictionary)
signal asteroid_contact_received(contact: Dictionary)
signal status_changed(message: String)
signal connection_changed(connected: bool)
# Read-only telemetry for an operator-initiated physical acceptance session.
signal flight_command_sent(request_id: int, verb: String)
signal flight_command_result(request_id: int, verb: String, disposition: String, elapsed_ms: int, outcome: int)
signal equipment_result_received(kind: String, request_id: int, outcome: int, command_id: int)
signal asteroid_survey_received(observation: Dictionary)
signal colony_onboarding_received(snapshot: Dictionary)

const MAX_LINE := 256
var _current_original_save := ""
var _pid := -1
var _last_shutdown_pid := -1
var _last_shutdown_msec := -10000
var _stdout: FileAccess
var _stderr: FileAccess
var _active := false
var _session := 0
var _sequence := -1
var _ship := 0
var _initial_ship_seen := false
var _last_ship_telemetry_request := -1
var _last_contact_request := -1
var _last_nav_request := -1
var _last_onboarding_request := -1
var _dock_state := -1
var _flight_cycle := 0
var _seat_sequence := -1
var _seated := false
var _pending_kind := ""
var _pending_flight_verb := ""
var _current_rock_id := 0
var _drill_online := false
var _observed_cargo := 0
var _survey_batch: Dictionary = {}
var _generation := -1
var _tick := -1
var _next_request := 1
var _pending := 0
var _last_send_msec := -10000
var _buffer := PackedByteArray()

func is_active() -> bool:
	return _active

func current_original_save() -> String:
	return _current_original_save if _active else ""

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
	var f := FileAccess.open("user://sg_native_player_status.log", FileAccess.READ_WRITE)
	if f == null:
		f = FileAccess.open("user://sg_native_player_status.log", FileAccess.WRITE)
	if f != null:
		f.seek_end()
		f.store_line(Time.get_datetime_string_from_system() + " | " + message)
		f.close()

func _host_path() -> String:
	var binary := "galaxy_desktop_player_host.exe" if OS.get_name() == "Windows" else "galaxy_desktop_player_host"
	return SGDX_LAUNCH.native_host_path(binary)

func start(save_override: String = "") -> void:
	if _active:
		return
	# A previous QUIT may be delayed by a blocked host or SQLite flush.
	# Do not start a second writer for this role while its SGDX custodian lives.
	if _last_shutdown_pid > 0:
		if OS.is_process_running(_last_shutdown_pid):
			_status("Previous SGDX writer still alive; refusing overlapping native session")
			return
		_last_shutdown_pid = -1
	var path := _host_path()
	if not FileAccess.file_exists(path):
		_status("NATIVE PLAYER HOST MISSING: " + path)
		return
	var save := save_override if not save_override.is_empty() else OS.get_user_data_dir().path_join("single_galaxy_authoritative.sqlite")
	var spec := SGDX_LAUNCH.supervised_command(save, "godot_ship", path, PackedStringArray([save]))
	if spec.is_empty():
		_status("SGDX supervisor missing or invalid save path; refusing unsupervised cockpit")
		return
	var result := OS.execute_with_pipe(spec["path"], spec["args"], false)
	if result.is_empty() or not result.has("stdio") or not result.has("pid"):
		_status("NATIVE PLAYER HOST LAUNCH FAILED")
		return
	_current_original_save = save
	_stdout = result["stdio"]
	_stderr = result.get("stderr")
	_pid = result["pid"]
	_active = true
	_session = 0
	_sequence = -1
	_ship = 0
	_initial_ship_seen = false
	_last_ship_telemetry_request = -1
	_last_contact_request = -1
	_last_nav_request = -1
	_last_onboarding_request = -1
	_dock_state = -1
	_flight_cycle = 0
	_seat_sequence = -1
	_seated = false
	_pending_kind = ""
	_pending_flight_verb = ""
	_current_rock_id = 0
	_drill_online = false
	_observed_cargo = 0
	_survey_batch.clear()
	_generation = -1
	_tick = -1
	_next_request = 1
	_pending = 0
	_last_send_msec = -10000
	_buffer.clear()
	connection_changed.emit(true)
	_status("P SGDX-SUPERVISED PLAYER: waiting for original POSE; no local walking")

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
		var rejected_verb := _pending_flight_verb if request == _pending and _pending_kind == "FLIGHT" else ""
		var rejected_equipment := _pending_kind if request == _pending and _pending_kind in ["MINE","FIT","UNFIT","FAB","POWER","ONBUILD"] else ""
		if request == _pending:
			if _pending_kind == "FLIGHT":
				flight_command_result.emit(request, _pending_flight_verb, "DENY",
					maxi(0, Time.get_ticks_msec() - _last_send_msec), -1)
				_pending_flight_verb = ""
			if not rejected_equipment.is_empty():
				equipment_result_received.emit(rejected_equipment,request,-1,0)
			_pending = 0
			_pending_kind = ""
		var message := "Original C++ refused %s action #%d: %s" % [
			rejected_verb if not rejected_verb.is_empty() else
			rejected_equipment if not rejected_equipment.is_empty() else "native", request, parts[3]]
		if parts[3] == "native-flight-refused":
			if rejected_verb == "BRAKE":
				message = "BRAKE refused: already stopped/below native braking threshold or custody check denied"
			elif rejected_verb == "DCK":
				message = "DOCK refused: must be stationary within 2.5m of an original station in the same frame"
			elif rejected_verb == "ASC" or rejected_verb == "DES":
				message = "Vertical control refused: this saved world may lack native 3D physics or custody"
		_status(message)
		return
	if parts.size()>0 and parts[0]=="ONBOARD":
		_accept_colony_onboarding(parts)
		return
	if parts.size() > 0 and parts[0] in ["SHEAD","SROW","SEND"]:
		_accept_original_survey(parts)
		return
	if parts.size() > 0 and (parts[0] == "NAV" or parts[0] == "NAVNONE"):
		_accept_original_station_nav(parts)
		return
	if parts.size() > 0 and (parts[0] == "ROCK" or parts[0] == "ROCKNONE"):
		_accept_original_asteroid(parts)
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
		if not _initial_ship_seen or _pending_kind not in ["FLIGHT","MINE","FIT","UNFIT","FAB","POWER","ONBUILD"] or request != _pending or \
			command_id < 1152921504606846976 or tick <= _tick or generation <= _generation:
			stop("Unrequested or stale native flight acknowledgement")
			return
		var done_verb := _pending_flight_verb
		var done_kind := _pending_kind
		var elapsed_ms := maxi(0, Time.get_ticks_msec() - _last_send_msec)
		_pending = 0
		_pending_kind = ""
		_pending_flight_verb = ""
		_tick = tick
		_generation = generation
		if done_kind == "FLIGHT":
			flight_command_result.emit(request, done_verb, "ACK" if outcome == 0 else "OUTCOME",
				elapsed_ms, outcome)
		else:
			equipment_result_received.emit(done_kind,request,outcome,command_id)
			_status("Original %s command #%d %s; verified cargo/ore snapshot follows" % [
				done_kind,request,"APPLIED" if outcome == 0 else "REFUSED outcome=%d" % outcome])
	_last_ship_telemetry_request = request
	_dock_state = dock
	ship_state_received.emit({"ship":ship,"command_id":command_id,"outcome":outcome,
		"tick":tick,"gen":generation,"x_mm":px,"y_mm":py,"z_mm":pz,
		"vx":vx,"vy":vy,"vz":vz,"heading":heading,"dock":dock})
	if request != 0 and outcome != 0:
		_status("Original ship command rejected with outcome code %d (native journal retained)" % outcome)

func _accept_colony_onboarding(parts: PackedStringArray) -> void:
	# Exactly one original-world inventory receipt following a paired SHIP.
	# These native materials are never a client-side construction ledger.
	if parts.size()!=17 or parts[1]!="1":
		stop("Malformed original colony construction source")
		return
	for i in range(2,17):
		if not _integer(parts[i]) or int(parts[i])<0:
			stop("Invalid original construction material source")
			return
	var request := int(parts[3])
	if int(parts[2])!=_session or int(parts[4])!=_ship or \
			request!=_last_ship_telemetry_request or \
			request<=_last_onboarding_request or \
			int(parts[5])!=_tick or int(parts[6])!=_generation or \
			int(parts[7])>1000000000 or int(parts[8])>1000000000 or \
			int(parts[9])>1000000000 or int(parts[10])>1000000000000 or \
			int(parts[11])>1000000000000 or \
			parts[13] not in ["0","1"] or parts[14] not in ["0","1"] or \
			parts[15] not in ["0","1"] or parts[16] not in ["0","1"]:
		stop("Original colony ship source/generation mismatch")
		return
	_last_onboarding_request=request
	colony_onboarding_received.emit({
		"ship":int(parts[4]),"request":request,"tick":_tick,
		"gen":_generation,"cargo_tonnes":int(parts[7]),
		"refined_tonnes":int(parts[8]),"modules":int(parts[9]),
		"plagioclase_kg":int(parts[10]),"aluminum_kg":int(parts[11]),
		"constructed_site":int(parts[12]),
		"can_refine":parts[13]=="1","can_modules":parts[14]=="1",
		"can_outpost":parts[15]=="1","can_aluminum":parts[16]=="1"})

func _accept_original_asteroid(parts: PackedStringArray) -> void:
	# One snapshot per verified original SHIP tick; never an independent oracle.
	var present := parts[0] == "ROCK"
	# v125 appends 2 original-contact receipt fields; older 04A worlds keep
	# the original packet exactly. Never infer physics merely from Godot.
	if (present and parts.size() not in [17, 19]) or (not present and parts.size() not in [10, 12]) or parts[1] != "1":
		stop("Malformed original-world asteroid frame")
		return
	for i in range(2, parts.size()):
		if not _integer(parts[i]):
			stop("Non-numeric native asteroid field")
			return
	var session := int(parts[2])
	var request := int(parts[3])
	var ship := int(parts[4])
	var tick := int(parts[5])
	var generation := int(parts[6])
	if session != _session or ship != _ship or tick != _tick or generation != _generation or request != _last_ship_telemetry_request or request <= _last_contact_request:
		stop("Stale or unrequested asteroid contact")
		return
	var ore := 0
	var rock_id := 0
	var dx := 0
	var dy := 0
	var dz := 0
	var radius := 0
	var taxonomy := 3
	var cargo := 0
	var capacity := 0
	var installed := 0
	if present:
		rock_id = int(parts[7])
		ore = int(parts[8])
		dx = int(parts[9])
		dy = int(parts[10])
		dz = int(parts[11])
		radius = int(parts[12])
		taxonomy = int(parts[13])
		cargo = int(parts[14])
		capacity = int(parts[15])
		installed = int(parts[16])
		if rock_id <= 0 or ore <= 0 or abs(dx) > 30000 or abs(dy) > 30000 or abs(dz) > 30000 or dx*dx+dy*dy+dz*dz > 900000000 or radius <= 0 or radius > 10000000 or taxonomy < 0 or taxonomy > 3 or _dock_state != 0:
			stop("Asteroid contact invalid or impossible for original ship")
			return
	else:
		cargo = int(parts[7])
		capacity = int(parts[8])
		installed = int(parts[9])
	var impact_rock := 0
	var impact_tick := 0
	if parts.size() == 19 or parts.size() == 12:
		var impact_index := 17 if present else 10
		impact_rock = int(parts[impact_index])
		impact_tick = int(parts[impact_index + 1])
		if impact_rock < 0 or ((impact_rock == 0) != (impact_tick == 0)) or 			(impact_rock > 0 and (impact_rock == ship or impact_tick != tick or _dock_state != 0)):
			stop("Unverifiable original solid-contact receipt")
			return
	if cargo < 0 or capacity < 0 or cargo > capacity or capacity > 1000000000 or installed not in [0, 1]:
		stop("Native ore stock or mining tool status invalid")
		return
	_last_contact_request = request
	_current_rock_id = rock_id if present else 0
	_drill_online = installed == 1
	_observed_cargo = cargo
	asteroid_contact_received.emit({"found":present,"rock":rock_id,
		"ore_tonnes":ore,"dx_mm":dx,"dy_mm":dy,"dz_mm":dz,"radius_mm":radius,
		"taxonomy":taxonomy,"ship":ship,"tick":tick,"generation":generation,
		"cargo_tonnes":cargo,"cargo_capacity":capacity,"mining_tool_online":installed,
		"impact_rock":impact_rock,"impact_tick":impact_tick})

func _accept_original_station_nav(parts: PackedStringArray) -> void:
	# C++ returns only stations in the original ship's same physical frame.
	# The UI neither invents a waypoint nor treats a target as docking approval.
	var found := parts[0] == "NAV"
	if (found and parts.size() != 11) or (not found and parts.size() != 7) or parts[1] != "1":
		stop("Invalid native station navigation frame")
		return
	for i in range(2, parts.size()):
		if not _integer(parts[i]):
			stop("Nonnumeric original station navigation frame")
			return
	var session := int(parts[2])
	var request := int(parts[3])
	var ship := int(parts[4])
	var tick := int(parts[5])
	var generation := int(parts[6])
	if session != _session or ship != _ship or tick != _tick or \
			generation != _generation or request != _last_ship_telemetry_request or \
			request != _last_contact_request or request <= _last_nav_request or \
			tick < 0 or generation < 1:
		stop("Stale or unpaired original station navigation")
		return
	var output := {"found":found,"ship":ship,"tick":tick,"gen":generation}
	if found:
		var station := int(parts[7])
		var dx := int(parts[8])
		var dy := int(parts[9])
		var dz := int(parts[10])
		const RANGE_MM := 1000000000
		if station <= 0 or abs(dx) > RANGE_MM or abs(dy) > RANGE_MM or \
				abs(dz) > RANGE_MM or dx*dx+dy*dy+dz*dz > RANGE_MM*RANGE_MM:
			stop("Original station navigation outside verified physical bounds")
			return
		output.merge({"station":station,"dx_mm":dx,"dy_mm":dy,"dz_mm":dz}, true)
	_last_nav_request = request
	station_nav_received.emit(output)

func _accept_original_survey(parts: PackedStringArray) -> void:
	var kind: String = parts[0]
	var expected_count := 9 if kind == "SHEAD" else 15 if kind == "SROW" else 7
	if parts.size() != expected_count or parts[1] != "1":
		stop("Original asteroid survey packet schema mismatch")
		return
	for i in range(2,parts.size()):
		if not _integer(parts[i]):
			stop("Non-numeric original asteroid survey packet")
			return
	var session := int(parts[2])
	var request := int(parts[3])
	var ship := int(parts[4])
	var tick := int(parts[5])
	var generation := int(parts[6])
	if (session != _session or ship != _ship or ship <= 0 or request != _pending or
			_pending_kind != "SURVEY" or tick != _tick or generation != _generation or
			_last_contact_request != _last_ship_telemetry_request or
			_last_nav_request != _last_ship_telemetry_request):
		stop("Unsolicited or stale original asteroid survey")
		return
	if kind == "SHEAD":
		if (not _survey_batch.is_empty() or int(parts[7]) < 0 or
				int(parts[7]) > 16 or int(parts[8]) not in [0,1]):
			stop("Duplicate or unbounded original asteroid survey")
			return
		_survey_batch = {"ship":ship,"tick":tick,"gen":generation,
			"request":request,"truncated":int(parts[8])==1,
			"count":int(parts[7]),"sources":[]}
		return
	if _survey_batch.is_empty() or _survey_batch.get("request",-1) != request:
		stop("Missing original asteroid survey header")
		return
	if kind == "SROW":
		var rock := int(parts[7])
		var dx := int(parts[8])
		var dy := int(parts[9])
		var dz := int(parts[10])
		var radius := int(parts[11])
		var ore := int(parts[12])
		var taxonomy := int(parts[13])
		var detail := int(parts[14])
		const MAX_RANGE_MM := 50000
		var rows: Array = _survey_batch["sources"]
		if (rock <= 0 or rock == ship or rows.size() >= int(_survey_batch["count"]) or
				abs(dx) > MAX_RANGE_MM or abs(dy) > MAX_RANGE_MM or
				abs(dz) > MAX_RANGE_MM or
				dx*dx+dy*dy+dz*dz > MAX_RANGE_MM*MAX_RANGE_MM or
				radius <= 0 or radius > 10000000 or taxonomy not in [0,1,2,3] or
				detail not in [0,1] or ore < -1 or ore > 1000000000000 or
				(detail == 0 and (ore != -1 or taxonomy != 3))):
			stop("Forged or unbounded original source asteroid")
			return
		for row in rows:
			if int(row["rock"]) == rock:
				stop("Repeated original asteroid ID within one survey")
				return
		rows.append({"rock":rock,"dx_mm":dx,"dy_mm":dy,"dz_mm":dz,
			"radius_mm":radius,"ore_tonnes":ore,"taxonomy":taxonomy,
			"geological_detail":detail==1})
		return
	var received: Array = _survey_batch["sources"]
	if received.size() != int(_survey_batch["count"]):
		stop("Incomplete original asteroid survey")
		return
	var result := _survey_batch.duplicate(true)
	_survey_batch.clear()
	_pending = 0
	_pending_kind = ""
	asteroid_survey_received.emit(result)
	_status("Original C++ surveyed %d confirmed asteroid silhouettes; ore geology only from powered scanner" % received.size())

func request_asteroid_survey() -> void:
	if not _active or _session <= 0 or not _seated or not _initial_ship_seen or _pending != 0:
		_status("Survey requires an original seated pilot and an idle native writer")
		return
	if (_last_contact_request != _last_ship_telemetry_request or
			_last_nav_request != _last_ship_telemetry_request):
		_status("Survey requires a current original SHIP/ROCK/NAV observation")
		return
	var now := Time.get_ticks_msec()
	if now - _last_send_msec < 110:
		return
	var req := _next_request
	_next_request += 1
	_pending = req
	_pending_kind = "SURVEY"
	_survey_batch.clear()
	_last_send_msec = now
	if not _stdout.store_line("SURVEY|%d|%d|%d|1" % [_session,req,_seat_sequence]):
		stop("Original asteroid survey pipe failed")
		return
	_stdout.flush()

func request_mine_one_tonne() -> void:
	# The last original ROCK telemetry provides a candidate, not authority.
	# Native C++ independently checks ownership, actual proximity, remaining
	# ore and cargo. Tool manufacturing is deferred; old source mode applies.
	_send_equipment("MINE",_current_rock_id)

func request_colony_ship_construction(operation: int) -> void:
	# Explicit user click; the original C++ nominates the exact fixed
	# recipe, checks mass/custody and later supplies a committed SHIP event.
	if operation<1 or operation>4:
		_status("Unsupported original physical ship construction action")
		return
	_send_equipment("ONBUILD",operation)

func request_fabricate_drill() -> void:
	_send_equipment("FAB",1)

func request_fit_drill() -> void:
	_send_equipment("FIT",1)

func request_unfit_drill() -> void:
	_send_equipment("UNFIT",1)

func request_drill_power(enabled: bool) -> void:
	_send_equipment("POWER",1 if enabled else 0)

func _send_equipment(kind: String, value: int) -> void:
	if not _active or _session <= 0 or not _seated or not _initial_ship_seen or _pending != 0:
		_status("Original fitting/mining requires seated pilot and an idle native writer")
		return
	if kind == "MINE" and (value <= 0 or
			_last_contact_request != _last_ship_telemetry_request):
		_status("Mining refused: no current original rock contact")
		return
	if kind not in ["MINE","FAB","FIT","UNFIT","POWER","ONBUILD"] or value < 0 or \
			(kind=="ONBUILD" and (value<1 or value>4)):
		_status("Invalid mining/equipment request")
		return
	var now := Time.get_ticks_msec()
	if now - _last_send_msec < 110:
		_status("Native equipment cooldown: retry after command interval")
		return
	var req := _next_request
	_next_request += 1
	_pending = req
	_pending_kind = kind
	_pending_flight_verb = ""
	_last_send_msec = now
	if not _stdout.store_line("%s|%d|%d|%d|%d" % [kind,_session,req,_seat_sequence,value]):
		stop("Original equipment/mining pipe write failed; reconnect to inspect SQLite")
		return
	_stdout.flush()

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
	_pending_flight_verb = verb
	_last_send_msec = now
	if not _stdout.store_line("FLIGHT|%d|%d|%d|%s" % [_session,req,_seat_sequence,verb]):
		stop("Native flight command pipe write failed; world state must be reverified")
		return
	_stdout.flush()
	flight_command_sent.emit(req, verb)

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

func stop(message: String = "Native player stopped; last committed pose retained", graceful: bool = false) -> void:
	if _pending != 0 and _pending_kind == "FLIGHT":
		flight_command_result.emit(_pending, _pending_flight_verb, "INTERRUPTED",
			maxi(0, Time.get_ticks_msec() - _last_send_msec), -1)
	_pending_flight_verb = ""
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
	_active = false
	_pid = -1
	_stdout = null
	_stderr = null
	_pending = 0
	_pending_kind = ""
	_survey_batch.clear()
	_last_onboarding_request=-1
	connection_changed.emit(false)
	_status(message)
