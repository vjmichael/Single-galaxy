extends RefCounted
## Human-readable SGDX health, from independently journaled native process
## receipts and already-verified original C++ world observations. Never
## scans Windows, opens crash dumps, changes SQLite or diagnoses without data.

const SCHEMA := "SGDX-OPERATOR-SNAPSHOT-01"
const MAX_RECEIPT_BYTES := 65536
const RECEIPT_LIMIT := 5

static func recent_supervisor_receipts(profile: String) -> Array[Dictionary]:
	var path := profile.path_join("single_galaxy_station_test.sqlite.diagnostics")
	var dir := DirAccess.open(path)
	var result: Array[Dictionary] = []
	if dir == null:
		return result
	var names: PackedStringArray = dir.get_files()
	names.sort()
	for i in range(names.size()-1,-1,-1):
		if result.size() >= RECEIPT_LIMIT:
			break
		var leaf: String = names[i]
		if not leaf.begins_with("process-") or not leaf.ends_with(".json"):
			continue
		var full := path.path_join(leaf)
		var stream := FileAccess.open(full,FileAccess.READ)
		if stream == null:
			continue
		if stream.get_length() > MAX_RECEIPT_BYTES:
			stream.close()
			continue
		var raw := stream.get_as_text()
		stream.close()
		var obj = JSON.parse_string(raw)
		if not (obj is Dictionary):
			continue
		# Restrict to whitelisted mechanical facts. No private absolute
		# paths, human text prompts, minidump bytes or full SQLite payload.
		result.append({
			"role":str(obj.get("role","unknown")).left(40),
			"reason":str(obj.get("reason","unknown")).left(70),
			"last_committed_tick":int(obj.get("last_committed_tick",-1)),
			"last_committed_generation":int(obj.get("last_committed_generation",-1)),
			"breadcrumb_count":int(obj.get("breadcrumb_count",-1))
		})
	return result

static func snapshot(profile: String, payload: Dictionary) -> Dictionary:
	var world := profile.path_join("single_galaxy_station_test.sqlite")
	return {
		"schema":SCHEMA,
		"wall_time_unix":Time.get_unix_time_from_system(),
		"world_save_exists":FileAccess.file_exists(world),
		"original_reset_pending":FileAccess.file_exists(profile.path_join("world-reset-pending.json")),
		"sgdx_recent":recent_supervisor_receipts(profile),
		"observed_native":payload
	}

static func human_summary(report: Dictionary) -> String:
	var obs: Dictionary = report.get("observed_native",{})
	var text := "SGDX ORIGINAL-WORLD HEALTH (READ ONLY)\n"
	text += "Original station SQLite: " + ("PRESENT" if report.get("world_save_exists",false) else "MISSING / new genesis pending") + "\n"
	if report.get("original_reset_pending",false):
		text += "WARNING: interrupted New Game recovery journal present; do not launch another writer\n"
	text += "Original station writer: " + ("CONNECTED" if obs.get("station_connected",false) else "DISCONNECTED") + "\n"
	text += "Original ship writer: " + ("CONNECTED" if obs.get("ship_connected",false) else "DISCONNECTED") + "\n"
	text += "Native ship tick: %d | generation: %d | dock state: %d\n" % [
		int(obs.get("tick",-1)),int(obs.get("generation",-1)),int(obs.get("dock",-1))]
	text += "Latest verified station range: " + (str(obs.get("return_range_m"))+" m" if obs.has("return_range_m") else "unavailable") + "\n"
	text += "GPU presentation: " + str(obs.get("adapter","unknown")) + "\n"
	text += "Draw distance: %.0f m | Godot FPS: %d | p95 %.1f ms\n" % [
		float(obs.get("draw_distance_m",0.0)),int(obs.get("fps",0)),float(obs.get("p95_ms",0.0))]
	text += "Recent SGDX supervisor receipts (newest first):\n"
	var receipts: Array = report.get("sgdx_recent",[])
	if receipts.is_empty():
		text += "  None recorded / never exited yet\n"
	for receipt in receipts:
		text += "  %s | %s | last tick %d\n" % [
			str(receipt.get("role","?")),str(receipt.get("reason","?")),
			int(receipt.get("last_committed_tick",-1))]
	text += "\nThis is diagnostics evidence, NOT game authority or a physical acceptance pass."
	return text

static func save_operator_snapshot(path: String, report: Dictionary) -> bool:
	if path.is_empty() or DirAccess.make_dir_recursive_absolute(path.get_base_dir()) != OK:
		return false
	var f := FileAccess.open(path,FileAccess.WRITE)
	if f == null:
		return false
	f.store_string(JSON.stringify(report,"\t"))
	f.flush()
	f.close()
	return true
