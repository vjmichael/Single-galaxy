extends PanelContainer
## GROUP3 — non-authoritative view/controller for the existing native original
## ship. Every button emits ONLY an intent; never changes ore, cargo, modules
## or the source SQLite world without a C++ verified SHIP/ROCK receipt.
signal survey_requested()
signal mine_requested()
signal fabricate_requested()
signal fit_requested()
signal unfit_requested()
signal power_requested(on: bool)
signal colony_ship_work_requested(operation: int)

var _status: Label
var _cargo: Label
var _survey: Label
var _mine: Button
var _survey_button: Button
var _buttons: Array[Button] = []
var _build_buttons: Array[Button] = []
var _build_status: Label
var _source_snapshot := {}
var _last_ship := {}
var _last_seated := false

func _ready() -> void:
	name = "NativeMiningAndFittingPanel"
	set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_RIGHT)
	offset_left = -382
	offset_right = -14
	offset_top = -425
	offset_bottom = -16
	visible = false
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.06,0.09,0.12,0.88)
	style.set_corner_radius_all(7)
	style.set_content_margin_all(9)
	add_theme_stylebox_override("panel", style)
	var column := VBoxContainer.new()
	column.custom_minimum_size = Vector2(344,0)
	column.add_theme_constant_override("separation",3)
	add_child(column)
	var heading := Label.new()
	heading.text = "RESOURCE COLLECTION | VERIFIED ORIGINAL SHIP"
	heading.add_theme_font_size_override("font_size",14)
	column.add_child(heading)
	_status = Label.new()
	_status.text = "Await original C++ ship and rock receipts"
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_status)
	_cargo = Label.new()
	_cargo.text = "Cargo: unavailable"
	column.add_child(_cargo)
	var actions := GridContainer.new()
	actions.columns = 3
	column.add_child(actions)
	_survey_button = _action(actions,"Survey (J)", func():survey_requested.emit(),
		"Read up to 50 metres only with original powered PLAY-11 scanner")
	_mine = _action(actions,"Mine 1t (M)", func():mine_requested.emit(),
		"Collect one finite tonne only from the exact original rock in reach")
	# Every industrial step below is an original ship action and committed
	# original-world event. This panel cannot award ore/modules/colonies.
	var build_title := Label.new()
	build_title.text = "COLONY FOUNDING | PHYSICAL ORE TO WORKSHOP"
	build_title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(build_title)
	_build_status = Label.new()
	_build_status.text = "Awaiting original source stock, position and constructed-site receipt"
	_build_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_build_status)
	var build_actions := GridContainer.new()
	build_actions.columns = 2
	column.add_child(build_actions)
	for item in [
		{"label":"Refine ≤4 t source ore","selector":1,
		 "hint":"Consumes up to four mined original cargo tonnes, producing real refined ore"},
		{"label":"Form ≤2 construction modules","selector":2,
		 "hint":"Consumes 2 tonnes original refined ore per real construction module"},
		{"label":"Build outpost · 4 modules","selector":3,
		 "hint":"Requires stationary ship in space, 4 original modules, original empty site and 3 crew"},
		{"label":"Separate plagioclase → aluminum","selector":4,
		 "hint":"Only if actual geological plagioclase stock exists; consumes that mineral into native aluminum and glass"}]:
		var b := _action(build_actions,str(item["label"]),
			func(): colony_ship_work_requested.emit(int(item["selector"])),
			str(item["hint"]))
		_build_buttons.append(b)
	_survey = Label.new()
	_survey.text = "Survey: requires fitted powered scanner (J)"
	_survey.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_survey)
	for button in _buttons:
		button.disabled = true

func _action(parent: GridContainer, title: String, callback: Callable, hint: String) -> Button:
	var button := Button.new()
	button.text = title
	button.tooltip_text = hint
	button.add_theme_font_size_override("font_size",12)
	button.pressed.connect(callback)
	parent.add_child(button)
	_buttons.append(button)
	return button

func disconnect_native() -> void:
	visible = false
	for button in _buttons:
		button.disabled = true
	_status.text = "Original native player disconnected"
	_survey.text = "Survey unavailable"
	_cargo.text = "Cargo unavailable"
	_source_snapshot.clear()
	_last_ship.clear()
	if is_instance_valid(_build_status):
		_build_status.text="Original colony ship disconnected"

func show_original_ship(ship: Dictionary, contact: Dictionary, seated: bool) -> void:
	_last_ship=ship.duplicate(true)
	_last_seated=seated
	var valid := not ship.is_empty() and ship.has("gen") and ship.has("tick")
	visible = valid
	var flying := valid and int(ship.get("dock",-1)) == 0
	var docked := valid and int(ship.get("dock",-1)) == 1
	var matched_contact := (valid and bool(contact.get("found",false)) and
		int(contact.get("tick",-1)) == int(ship.get("tick",-2)) and
		int(contact.get("ship",-1)) == int(ship.get("ship",-2)))
	_survey_button.disabled = not (valid and flying and seated)
	_mine.disabled = not (flying and seated and matched_contact)
	for button in _buttons:
		if button == _survey_button or button == _mine:
			continue
		# The native kernel checks inventory and actual module/crew power;
		# enabling a UI request does not imply these resources exist.
		button.disabled = not (valid and seated and (docked if button.text in ["Fit drill","Unfit","Fabricate test module"] else true))
	_status.text = "Original ship #%d | %s | source ore %s" % [
		int(ship.get("ship",0)),
		"DOCKED" if docked else "IN SPACE" if flying else "TRANSITION",
		"CONTACT VERIFIED" if matched_contact else "NO CONTACT"]
	if contact.is_empty() or int(contact.get("tick",-1)) != int(ship.get("tick",-2)):
		_cargo.text = "Cargo: waiting for original ROCK/ROCKNONE"
	else:
		_cargo.text = "Cargo: %d / %d physical tonnes" % [
			int(contact.get("cargo_tonnes",0)),int(contact.get("cargo_capacity",0))]
	_apply_native_build_options()

func set_original_colony_source(snapshot: Dictionary) -> void:
	# No optimistic local stock updates. Source is always an exact committed
	# ship/tick/generation pair supplied by native single-writer readback.
	_source_snapshot = snapshot.duplicate(true)
	_apply_native_build_options()

func _apply_native_build_options() -> void:
	if not is_instance_valid(_build_status):
		return
	var source_ok := not _source_snapshot.is_empty() and \
		int(_source_snapshot.get("ship",-1))==int(_last_ship.get("ship",-2)) and \
		int(_source_snapshot.get("tick",-1))==int(_last_ship.get("tick",-2)) and \
		int(_source_snapshot.get("gen",-1))==int(_last_ship.get("gen",-2))
	var names := ["can_refine","can_modules","can_outpost","can_aluminum"]
	for i in range(_build_buttons.size()):
		_build_buttons[i].disabled=not (source_ok and _last_seated and \
			bool(_source_snapshot.get(names[i],false)))
	if not source_ok:
		_build_status.text="Waiting for exact original ship + manufacturing source receipt"
		return
	var built := int(_source_snapshot.get("constructed_site",0))
	var line := "Physical stock: %dt raw | %dt refined | %d modules | %dkg plagioclase | %dkg aluminum" % [
		int(_source_snapshot.get("cargo_tonnes",0)),
		int(_source_snapshot.get("refined_tonnes",0)),
		int(_source_snapshot.get("modules",0)),
		int(_source_snapshot.get("plagioclase_kg",0)),
		int(_source_snapshot.get("aluminum_kg",0))]
	if built>0:
		line+="\nORIGINAL CONSTRUCTED COLONY #%d: dock there, leave ship, press I for real facilities, storage, training and 1-batch manufacturing." % built
	else:
		line+="\nHarvest finite rock, refine original ore and make 4 modules. In space, stop at least 2.5m clear of any station, then found an outpost."
	_build_status.text=line

func show_original_survey(result: Dictionary) -> void:
	var sources: Array = result.get("sources",[])
	var rows := PackedStringArray()
	rows.append("%d original asteroids within 50m%s" % [
		sources.size()," (scan truncated)" if bool(result.get("truncated",false)) else ""])
	for item in sources.slice(0,6):
		var delta := Vector3(float(item["dx_mm"]),float(item["dy_mm"]),float(item["dz_mm"]))
		var label := "rock #%d | %.1fm" % [int(item["rock"]),delta.length()/1000.0]
		if bool(item.get("geological_detail",false)):
			label += " | %dt original ore" % int(item["ore_tonnes"])
		else:
			label += " | geology UNKNOWN (scanner required)"
		rows.append(label)
	_survey.text = "\n".join(rows)

func note_original_outcome(kind: String, request: int, outcome: int, command: int) -> void:
	_status.text = "C++ %s #%d | %s | journal %d" % [
		kind,request,"APPLIED" if outcome == 0 else "DENIED code %d" % outcome,command]
