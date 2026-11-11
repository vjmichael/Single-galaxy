extends RefCounted
## One-use, human-confirmed New Game archive for the native Windows/Linux
## station pilot. No world is deleted or fabricated by the UI: the original
## station host creates a fresh SQLite world ONLY after a safe archive.
const WORLD_NAMES = [
	"single_galaxy_station_test.sqlite",
	"single_galaxy_authoritative.sqlite"
]
const WORLD_SUFFIXES = ["", "-wal", "-shm", "-journal", ".diagnostics"]
const JOURNAL := "world-reset-pending.json"
const ARCHIVE_FOLDER := "world-archives"
const SCHEMA := "SG-WORLD-RESET-01"

static func _files_and_dirs(root: String) -> Array[Dictionary]:
	var found: Array[Dictionary] = []
	for name in WORLD_NAMES:
		for suffix in WORLD_SUFFIXES:
			var leaf: String = name + suffix
			var path := root.path_join(leaf)
			if FileAccess.file_exists(path):
				found.append({"name":leaf,"dir":false})
			elif DirAccess.dir_exists_absolute(path) and suffix == ".diagnostics":
				found.append({"name":leaf,"dir":true})
	return found

static func _write_json(path: String, data: Dictionary) -> bool:
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return false
	f.store_string(JSON.stringify(data, "\t"))
	f.flush()
	f.close()
	return true

static func _rollback(root: String, archive: String, entries: Array[Dictionary]) -> bool:
	# Reversal is fail-closed: never overwrite a fresh world from elsewhere.
	for i in range(entries.size() - 1, -1, -1):
		var name := str(entries[i]["name"])
		var old := root.path_join(name)
		var backup := archive.path_join(name)
		if FileAccess.file_exists(backup) or DirAccess.dir_exists_absolute(backup):
			if FileAccess.file_exists(old) or DirAccess.dir_exists_absolute(old):
				return false
			if DirAccess.rename_absolute(backup, old) != OK:
				return false
		elif not (FileAccess.file_exists(old) or DirAccess.dir_exists_absolute(old)):
			# A missing source AND backup is data loss, not successful recovery.
			return false
	return true

static func recover_pending(root: String, running_pids: Array[int]) -> Dictionary:
	for pid in running_pids:
		if pid > 0 and OS.is_process_running(pid):
			return {"ok":false,"reason":"Live SGDX writer prevents reset recovery"}
	var journal_path := root.path_join(JOURNAL)
	if not FileAccess.file_exists(journal_path):
		return {"ok":true,"recovered":false}
	var obj = JSON.parse_string(FileAccess.get_file_as_string(journal_path))
	if not (obj is Dictionary) or obj.get("schema", "") != SCHEMA or \
			not (obj.get("entries", null) is Array) or \
			not (obj.get("archive", null) is String):
		return {"ok":false,"reason":"Invalid reset recovery journal. World preserved; manual review required."}
	var archive := str(obj["archive"])
	# Never trust a stale/corrupted journal to rename outside this profile.
	if not archive.begins_with(root.path_join(ARCHIVE_FOLDER) + "/"):
		return {"ok":false,"reason":"Reset journal archive escapes the app profile; refusing recovery"}
	var entries: Array[Dictionary] = []
	for item in obj["entries"]:
		if not (item is Dictionary) or not (item.get("name", null) is String):
			return {"ok":false,"reason":"Invalid reset journal item"}
		var name := str(item["name"])
		var allowed := false
		for w in WORLD_NAMES:
			for suffix in WORLD_SUFFIXES:
				if name == w + suffix:
					allowed = true
		if not allowed:
			return {"ok":false,"reason":"Reset journal contains an unrecognized world file"}
		entries.append({"name":name})
	if not _rollback(root, archive, entries):
		return {"ok":false,"reason":"Interrupted reset could not restore every original file; do not create a new world"}
	if DirAccess.remove_absolute(journal_path) != OK:
		return {"ok":false,"reason":"Original world restored but reset journal cannot be removed"}
	return {"ok":true,"recovered":true,"reason":"Interrupted New Game was rolled back; original saved world restored"}

static func archive_for_new_game(root: String, running_pids: Array[int], fault_after_moves: int = -1) -> Dictionary:
	# This is a human-confirmed action only; never an automatic corruption
	# fix, game retry, GitHub test gate, or model-generated approval.
	for pid in running_pids:
		if pid > 0 and OS.is_process_running(pid):
			return {"ok":false,"reason":"Live native supervisor still owns this world"}
	if not DirAccess.dir_exists_absolute(root):
		return {"ok":false,"reason":"Godot user-data directory missing"}
	if FileAccess.file_exists(root.path_join(JOURNAL)):
		return {"ok":false,"reason":"Incomplete prior reset; recover original world first"}
	var entries := _files_and_dirs(root)
	var archive := root.path_join(ARCHIVE_FOLDER).path_join(
		"world-" + str(int(Time.get_unix_time_from_system())) + "-" + str(Time.get_ticks_usec()))
	if DirAccess.make_dir_recursive_absolute(archive) != OK:
		return {"ok":false,"reason":"Cannot create a private dated world archive"}
	var journal := {"schema":SCHEMA, "archive":archive, "entries":entries,
		"started_unix":Time.get_unix_time_from_system(),
		"policy":"keep all original SQLite and sidecars; no deletion"}
	if not _write_json(root.path_join(JOURNAL), journal):
		return {"ok":false,"reason":"Reset recovery journal could not be persisted"}
	var moved: Array[Dictionary] = []
	for item in entries:
		var name := str(item["name"])
		var source := root.path_join(name)
		var destination := archive.path_join(name)
		if fault_after_moves >= 0 and moved.size() == fault_after_moves:
			if _rollback(root, archive, moved):
				DirAccess.remove_absolute(root.path_join(JOURNAL))
			return {"ok":false,"reason":"Injected archive interruption; checked rollback"}
		if DirAccess.rename_absolute(source, destination) != OK:
			var rolled_back := _rollback(root, archive, moved)
			if rolled_back:
				DirAccess.remove_absolute(root.path_join(JOURNAL))
			return {"ok":false,"reason":"Could not archive " + name +
				("; prior files restored" if rolled_back else "; recovery journal retained")}
		moved.append(item)
	if not _write_json(archive.path_join("reset-manifest.json"), journal):
		var rolled_back := _rollback(root, archive, moved)
		if rolled_back:
			DirAccess.remove_absolute(root.path_join(JOURNAL))
		return {"ok":false,"reason":"Archive manifest failed" +
			("; original world restored" if rolled_back else "; recovery journal retained")}
	if DirAccess.remove_absolute(root.path_join(JOURNAL)) != OK:
		return {"ok":false,"reason":"Archive preserved but pending journal remains; new world blocked"}
	return {"ok":true,"archive":archive,"entries":entries.size(),
		"reason":"Original world archived, not deleted"}
