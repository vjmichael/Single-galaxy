extends RefCounted
## SGDX-03: One supervised path for every native Godot host.
## The supervisor MUST be present; never bypass custody by launching a
## writer directly. It transparently inherits Godot stdin/stdout. Diagnostics
## are private per-save sidecars, NOT SHIP/ROCK/POSE protocol frames.

static func _candidate(name: String) -> String:
	var project := ProjectSettings.globalize_path("res://native/" + name)
	if FileAccess.file_exists(project):
		return project
	var adjacent := OS.get_executable_path().get_base_dir().path_join("native").path_join(name)
	if FileAccess.file_exists(adjacent):
		return adjacent
	return ""

static func native_host_path(name: String) -> String:
	return _candidate(name)

static func supervised_command(save: String, role: String, host: String, host_args: PackedStringArray) -> Dictionary:
	if host.is_empty() or not FileAccess.file_exists(host) or not save.is_absolute_path():
		return {}
	var binary := "galaxy_server_dx_supervisor.exe" if OS.get_name() == "Windows" else "galaxy_server_dx_supervisor"
	var supervisor := _candidate(binary)
	if supervisor.is_empty():
		return {} # never silently weaken supervision
	var args := PackedStringArray(["--save", save, "--role", role, "--interactive", "--record-clean"])
	# Second-chance Windows dumps may contain sensitive process memory.
	# This is intentionally disabled unless the user opts in explicitly.
	if OS.get_name() == "Windows" and OS.get_environment("SGDX_WINDOWS_MINIDUMP") == "1":
		args.append("--windows-minidump")
	var manifest := _candidate("SGDX_BUILD_SHA.txt")
	if not manifest.is_empty():
		var f := FileAccess.open(manifest, FileAccess.READ)
		if f != null:
			var sha := f.get_as_text().strip_edges()
			if sha.length() == 40 and sha.is_valid_hex_number():
				args.append_array(PackedStringArray(["--build-sha", sha]))
	args.append("--")
	args.append(host)
	args.append_array(host_args)
	return {"path": supervisor, "args": args}
