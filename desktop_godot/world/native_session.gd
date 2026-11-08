extends RefCounted
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

func _init() -> void:
	_parser = PROJECTION_SCRIPT.new()
	_parser.snapshot_received.connect(func(frame: Dictionary): native_snapshot.emit(frame))
	_parser.rejected.connect(func(reason: String): stop("Native stream rejected: " + reason))

func _host_path() -> String:
	var name := "galaxy_desktop_readonly_host.exe" if OS.get_name() == "Windows" else "galaxy_desktop_readonly_host"
	# Development run: source-directory native/; exported runtime: adjacent native/.
	if OS.has_feature("editor"):
		return ProjectSettings.globalize_path("res://native/" + name)
	return OS.get_executable_path().get_base_dir().path_join("native").path_join(name)

func start() -> void:
	if _active:
		return
	var path := _host_path()
	if not FileAccess.file_exists(path):
		status_changed.emit("Native host not bundled; this is still a local movement prototype")
		return
	var save := OS.get_user_data_dir().path_join("fp3d_native_readonly.sqlite")
	_parser.reset()
	var result := OS.execute_with_pipe(path, PackedStringArray([save, "120"]), false)
	if result.is_empty() or not result.has("stdio") or not result.has("pid"):
		status_changed.emit("Native host could not start")
		return
	_stdio = result["stdio"]
	_stderr = result.get("stderr")
	_pid = result["pid"]
	_active = true
	status_changed.emit("Native C++ telemetry connecting (READ ONLY)")

func poll() -> void:
	if not _active:
		return
	if not OS.is_process_running(_pid):
		# Drain remaining stdio bytes before marking closed.
		_drain()
		stop("Native feed ended; no game commands were sent")
		return
	_drain()

func _drain() -> void:
	if _stdio == null:
		return
	# Pipes created non-blocking; read a bounded amount each render frame.
	var buffer := _stdio.get_buffer(4096)
	if not buffer.is_empty():
		_parser.accept_bytes(buffer)

func stop(message: String = "Native telemetry disconnected") -> void:
	if _active and _pid > 0 and OS.is_process_running(_pid):
		OS.kill(_pid) # child is only the read-only observer
	_active = false
	_pid = -1
	_stdio = null
	_stderr = null
	status_changed.emit(message)
