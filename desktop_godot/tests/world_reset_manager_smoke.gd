extends SceneTree
## Godot 4.7.2 real filesystem acceptance. All files here are explicitly
## synthetic test bytes under an isolated throwaway directory, NOT user saves.
const RESET := preload("res://world/world_reset_manager.gd")
func _initialize() -> void:
	call_deferred("_run")
func _fail(reason: String) -> void:
	push_error("SG-RESET-01 FAIL: "+reason)
	quit(1)
func _put(path: String, value: String) -> bool:
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return false
	f.store_string(value)
	f.close()
	return true
func _run() -> void:
	var root := OS.get_user_data_dir().path_join("reset-smoke-" + str(Time.get_ticks_usec()))
	if DirAccess.make_dir_recursive_absolute(root) != OK:
		_fail("cannot create isolated test profile")
		return
	var world := root.path_join("single_galaxy_station_test.sqlite")
	var wal := world + "-wal"
	var original := "SYNTHETIC_SQLITE_WORLD_BYTE_CONTRACT_219"
	var wal_bytes := "SYNTHETIC_WAL_BYTES_17"
	if not _put(world, original) or not _put(wal, wal_bytes):
		_fail("test fixture file writes failed")
		return
	var live_pid: Array[int] = [OS.get_process_id()]
	var denied: Dictionary = RESET.archive_for_new_game(root, live_pid)
	if denied.get("ok", true) or FileAccess.get_file_as_string(world) != original:
		_fail("live native writer guard failed; original bytes changed")
		return
	var empty_pids: Array[int] = []
	var interrupted: Dictionary = RESET.archive_for_new_game(root, empty_pids, 1)
	if interrupted.get("ok", true) or FileAccess.get_file_as_string(world) != original or \
			FileAccess.get_file_as_string(wal) != wal_bytes:
		_fail("injected partial archive did not roll back both original files")
		return
	if FileAccess.file_exists(root.path_join(RESET.JOURNAL)):
		_fail("rolled-back archive left a misleading reset pending journal")
		return
	var result: Dictionary = RESET.archive_for_new_game(root, empty_pids)
	if not result.get("ok", false) or int(result.get("entries", 0)) != 2 or \
			FileAccess.file_exists(world) or FileAccess.file_exists(wal):
		_fail("confirmed archive did not isolate the original world and WAL")
		return
	var archive := str(result["archive"])
	if FileAccess.get_file_as_string(archive.path_join(world.get_file())) != original or \
			FileAccess.get_file_as_string(archive.path_join(wal.get_file())) != wal_bytes:
		_fail("original source bytes not preserved in dated backup")
		return
	if not FileAccess.file_exists(archive.path_join("reset-manifest.json")) or \
			FileAccess.file_exists(root.path_join(RESET.JOURNAL)):
		_fail("archive manifest or reset journal lifecycle incorrect")
		return
	# A new game cannot mutate or silently reset the prior archive; it will
	# be created later by the authoritative native station host, not here.
	if not _put(world,"NEW_SYNTHETIC_WORLD_ONLY"):
		_fail("test could not create a separate fresh file")
		return
	if FileAccess.get_file_as_string(archive.path_join(world.get_file())) != original:
		_fail("old archive was overwritten by new game file")
		return
	# Simulate an app crash AFTER original file is moved but BEFORE the
	# durable pending journal is cleared. Restart must restore old state,
	# not mistakenly create another genesis world.
	var crash_root := root.path_join("crash-case")
	var crash_arch := crash_root.path_join(RESET.ARCHIVE_FOLDER).path_join("incomplete")
	if DirAccess.make_dir_recursive_absolute(crash_arch) != OK or \
			not _put(crash_arch.path_join(world.get_file()), "CRASH_OLD"):
		_fail("interrupted reset fixture could not be created")
		return
	var pending := {"schema":RESET.SCHEMA,"archive":crash_arch,
		"entries":[{"name":world.get_file(),"dir":false}]}
	if not _put(crash_root.path_join(RESET.JOURNAL),JSON.stringify(pending)):
		_fail("recovery journal fixture missing")
		return
	var recovered: Dictionary = RESET.recover_pending(crash_root, empty_pids)
	if not recovered.get("ok", false) or not recovered.get("recovered", false) or \
			FileAccess.get_file_as_string(crash_root.path_join(world.get_file())) != "CRASH_OLD":
		_fail("restart recovery did not rehydrate original world from journal")
		return
	if FileAccess.file_exists(crash_root.path_join(RESET.JOURNAL)):
		_fail("successful recovery retained a false stale journal")
		return
	print("SG-RESET-01 REAL GODOT FILESYSTEM PASS: consent boundary, live writer denial, WAL archive, rollback, durable interrupted reset repair, original bytes unchanged")
	quit(0)
