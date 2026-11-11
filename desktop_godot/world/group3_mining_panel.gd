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

var _status: Label
var _cargo: Label
var _survey: Label
var _mine: Button
var _survey_button: Button
var _buttons: Array[Button] = []

func _ready() -> void:
	name = "NativeMiningAndFittingPanel"
	set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_RIGHT)
	offset_left = -382
	offset_right = -14
	offset_top = -284
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
	heading.text = "MINING / FITTING | VERIFIED ORIGINAL SHIP"
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
		"Request exactly 1 tonne in the original native simulation; never free ore")
	_action(actions,"Fabricate test module", func():fabricate_requested.emit(),
		"Requires real ship stock equivalent to 2 refined tonnes and crew")
	_action(actions,"Fit drill", func():fit_requested.emit(),
		"Requires an actual docked ship, an empty original hardpoint and a manufactured module")
	_action(actions,"Unfit", func():unfit_requested.emit(),
		"Returns the original 2-tonne-equivalent installed drill to physical ship inventory")
	_action(actions,"Power ON", func():power_requested.emit(true),
		"Actual installed hardpoint and trusted native power state only")
	_action(actions,"Power OFF", func():power_requested.emit(false),
		"Send a physical rack power request; cannot conjure new equipment")
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

func show_original_ship(ship: Dictionary, contact: Dictionary, seated: bool) -> void:
	var valid := not ship.is_empty() and ship.has("gen") and ship.has("tick")
	visible = valid
	var flying := valid and int(ship.get("dock",-1)) == 0
	var docked := valid and int(ship.get("dock",-1)) == 1
	var matched_contact := (valid and bool(contact.get("found",false)) and
		int(contact.get("tick",-1)) == int(ship.get("tick",-2)) and
		int(contact.get("ship",-1)) == int(ship.get("ship",-2)))
	var powered_drill := matched_contact and bool(contact.get("mining_tool_online",false))
	_survey_button.disabled = not (valid and flying and seated)
	_mine.disabled = not (flying and seated and powered_drill)
	for button in _buttons:
		if button == _survey_button or button == _mine:
			continue
		# The native kernel checks inventory and actual module/crew power;
		# enabling a UI request does not imply these resources exist.
		button.disabled = not (valid and seated and (docked if button.text in ["Fit drill","Unfit","Fabricate test module"] else true))
	_status.text = "Original ship #%d | %s | drill %s" % [
		int(ship.get("ship",0)),
		"DOCKED" if docked else "IN SPACE" if flying else "TRANSITION",
		"POWERED" if powered_drill else "UNFITTED/OFFLINE/UNVERIFIED"]
	if contact.is_empty() or int(contact.get("tick",-1)) != int(ship.get("tick",-2)):
		_cargo.text = "Cargo: waiting for original ROCK/ROCKNONE"
	else:
		_cargo.text = "Cargo: %d / %d physical tonnes" % [
			int(contact.get("cargo_tonnes",0)),int(contact.get("cargo_capacity",0))]

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
