extends RefCounted
const SGDX_LAUNCH := preload("res://world/sgdx_process_launch.gd")
## FP3D-02B.0: opt-in child stdio pipe, NO browser, HTTP or Python.
## Only reads a native host; no game command is forwarded yet.
signal native_snapshot(frame: Dictionary)
signal status_changed(message: String)
const PROJECTION_SCRIPT := preload("res://world/native_projection.gd")
var _pid := -1
var _stdio: FileAccess
var _stderr: FileAccess
var _parser
var _active := false
var _last_verified_tick := -1
var _verified_packets := 0
# These are user-visible diagnostics, not game authority.
var _last_status := "Native telemetry has not been started"

func _emit_status(message: String) -> void:
	_last_status = message
	status_changed.emit(message)
	# Status changes persist so Windows test evidence does not require a screenshot.
	var f := FileAccess.open("user://sg_native_bridge_status.log", FileAccess.READ_WRITE)
	if f == null:
		f = FileAccess.open("user://sg_native_bridge_status.log", FileAccess.WRITE)
	if f != null:
		f.seek_end()
		f.store_line(Time.get_datetime_string_from_system() + " | " + message)
		f.close()

func is_active() -> bool:
	return _active

func _receive_verified(frame: Dictionary) -> void:
	_last_verified_tick = int(frame["tick"])
	_verified_packets += 1
	native_snapshot.emit(frame)

func _init() -> void:
	_parser = PROJECTION_SCRIPT.new()
	_parser.snapshot_received.connect(_receive_verified)
	_parser.rejected.connect(func(reason: String): stop("Native stream rejected: " + reason))

func _host_path() -> String:
	var name := "galaxy_desktop_readonly_host.exe" if OS.get_name() == "Windows" else "galaxy_desktop_readonly_host"
	return SGDX_LAUNCH.native_host_path(name)

func start() -> void:
	if _active:
		return
	var path := _host_path()
	if not FileAccess.file_exists(path):
		_emit_status("Native host missing at: " + path + " | O retry")
		return
	var save := OS.get_user_data_dir().path_join("fp3d_native_readonly.sqlite")
	_parser.reset()
	_last_verified_tick = -1
	_verified_packets = 0
	var spec := SGDX_LAUNCH.supervised_command(save, "godot_observer", path,
		PackedStringArray([save, "600"]))
	if spec.is_empty():
		_emit_status("SGDX supervisor unavailable: refusing unsupervised native observer")
		return
	var result := OS.execute_with_pipe(spec["path"], spec["args"], false)
	if result.is_empty() or not result.has("stdio") or not result.has("pid"):
		_emit_status("Native host could not start | O retry; see user://sg_native_bridge_status.log")
		return
	_stdio = result["stdio"]
	_stderr = result.get("stderr")
	_pid = result["pid"]
	_active = true
	_emit_status("READ ONLY — native C++ telemetry connecting; O stops")

func poll() -> void:
	if not _active:
		return
	_drain()
	if _active and not OS.is_process_running(_pid):
		# The host is intentionally bounded. Never erase the verified last frame.
		var reason := "READ ONLY — native stream completed at tick %d (%d verified packets); O reconnects" % [
			_last_verified_tick, _verified_packets]
		if _verified_packets == 0:
			reason = "READ ONLY — native host ended with no verified frames; see status log; O retry"
		var detail := _read_stderr()
		if not detail.is_empty():
			reason += " | native error: " + detail
		stop(reason)

func _drain() -> void:
	if _stdio == null:
		return
	# Pipes are nonblocking; drain up to 64 KiB each render frame. On Windows,
	# a terminated child can leave several frames buffered after process exit.
	for i in range(16):
		var buffer := _stdio.get_buffer(4096)
		if buffer.is_empty():
			break
		_parser.accept_bytes(buffer)
		if not _active:
			break

func _read_stderr() -> String:
	if _stderr == null:
		return ""
	var data := _stderr.get_buffer(1024)
	var raw := data.get_string_from_utf8()
	raw = raw.replace("\r", " ").replace("\n", " ").replace("\t", " ")
	return raw.substr(0, 220)

func stop(message: String = "Native telemetry disconnected") -> void:
	if _active and _pid > 0 and OS.is_process_running(_pid):
		OS.kill(_pid) # child is only the read-only observer
	_active = false
	_pid = -1
	_stdio = null
	_stderr = null
	_emit_status(message)
