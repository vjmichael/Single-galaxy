extends SceneTree
## SGDX diagnosis is deliberately read-only and independent of user SQLite.
const DX := preload("res://world/sgdx_operator_diagnostics.gd")
const MAIN := preload("res://world/main.gd")
func _initialize() -> void:
	call_deferred("_run")
func _fail(reason: String) -> void:
	push_error("SGDX-OPERATOR-01 FAIL: "+reason)
	quit(1)
func _run() -> void:
	var dir := OS.get_user_data_dir().path_join("sgdx-dx-smoke-"+str(Time.get_ticks_usec()))
	var receipts := dir.path_join("single_galaxy_station_test.sqlite.diagnostics")
	if DirAccess.make_dir_recursive_absolute(receipts) != OK:
		_fail("cannot create test-only SGDX sidecars")
		return
	var content := {"role":"godot_ship","reason":"clean_exit",
		"last_committed_tick":47,"breadcrumb_count":3,
		"private_absolute_path":"NEVER_EXPORT_THIS"}
	var f := FileAccess.open(receipts.path_join("process-0001.json"),FileAccess.WRITE)
	if f == null:
		_fail("cannot create isolated diagnostics fixture")
		return
	f.store_string(JSON.stringify(content))
	f.close()
	var observed := {"station_connected":false,"ship_connected":true,
		"tick":47,"generation":19,"dock":0,"adapter":"AMD Radeon AI Pro R9700",
		"draw_distance_m":8500.0,"fps":85,"p95_ms":13.0}
	var report: Dictionary = DX.snapshot(dir, observed)
	var recent: Array = report.get("sgdx_recent",[])
	if report.get("schema","") != DX.SCHEMA or recent.size()!=1 or \
			recent[0]["reason"] != "clean_exit" or \
			recent[0].has("private_absolute_path"):
		_fail("wrong original SGDX receipt or private data leaked")
		return
	var human := DX.human_summary(report)
	if not human.contains("last tick 47") or \
			not human.contains("8500 m") or human.contains("NEVER_EXPORT"):
		_fail("actual native evidence not translated to user-facing diagnosis")
		return
	var output := dir.path_join("diagnosis.json")
	if not DX.save_operator_snapshot(output, report):
		_fail("local diagnostic evidence write failed")
		return
	var saved = JSON.parse_string(FileAccess.get_file_as_string(output))
	if not (saved is Dictionary) or saved.get("schema","")!=DX.SCHEMA:
		_fail("saved SGDX report is invalid")
		return
	var ui = MAIN.new()
	if not ui.has_method("_show_sgdx_diagnostics"):
		_fail("actual game lacks player F5 diagnostics command")
		return
	print("SGDX-OPERATOR-01 GODOT PASS: authentic receipt schema, bounded read-only summary, sensitive-field redaction, local evidence; no private SQLite or physical approval")
	quit(0)
