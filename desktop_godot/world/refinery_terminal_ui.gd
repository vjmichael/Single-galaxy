extends PanelContainer
## PLAY-STATION-03C: strictly read-only display of the C++ station refinery.
## The ONLY emitted operation is the selected physical lot identifier.
signal refine_requested(lot_id: int)
signal unload_requested()
signal terminal_closed()

var _lots: ItemList
var _status: Label
var _minerals: Label
var _refine_button: Button
var _unload_button: Button
var _current := {}

func _ready() -> void:
	name = "NativeStationRefineryTerminal"
	position = Vector2(800, 110)
	custom_minimum_size = Vector2(450, 380)
	visible = false
	var column := VBoxContainer.new()
	add_child(column)
	var heading := Label.new()
	heading.text = "STATION REFINERY | ORIGINAL SQLITE"
	heading.add_theme_font_size_override("font_size", 18)
	column.add_child(heading)
	_status = Label.new()
	_status.text = "No native refinery information"
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_status)
	var lot_heading := Label.new()
	lot_heading.text = "Unloaded station ore lots (real source IDs)"
	column.add_child(lot_heading)
	_lots = ItemList.new()
	_lots.custom_minimum_size = Vector2(430, 115)
	_lots.item_selected.connect(_update_refine_button)
	column.add_child(_lots)
	_minerals = Label.new()
	_minerals.text = "Minerals: no native inventory received"
	column.add_child(_minerals)
	_unload_button = Button.new()
	_unload_button.text = "Unload 1 tonne from owned docked ship"
	_unload_button.tooltip_text = "Original C++ checks actual cargo, ownership, dock and terminal distance"
	_unload_button.disabled = true
	_unload_button.pressed.connect(_on_unload_pressed)
	column.add_child(_unload_button)
	_refine_button = Button.new()
	_refine_button.text = "Refine selected one-tonne lot"
	_refine_button.disabled = true
	_refine_button.pressed.connect(_on_refine_pressed)
	column.add_child(_refine_button)
	var close := Button.new()
	close.text = "Close terminal (R)"
	close.pressed.connect(func(): terminal_closed.emit())
	column.add_child(close)

func show_waiting() -> void:
	visible = true
	_current.clear()
	if is_instance_valid(_lots):
		_lots.clear()
	if is_instance_valid(_status):
		_status.text = "Contacting original C++ station host..."
	if is_instance_valid(_refine_button):
		_refine_button.disabled = true
	if is_instance_valid(_unload_button):
		_unload_button.disabled = true

func report_error(message: String) -> void:
	if is_instance_valid(_status):
		_status.text = message
	if is_instance_valid(_refine_button):
		_refine_button.disabled = true
	if is_instance_valid(_unload_button):
		_unload_button.disabled = true

func present(snapshot: Dictionary) -> void:
	# NEVER render optimistic ore or assign a local amount to inventory.
	_current = snapshot.duplicate(true)
	visible = true
	var installed := int(snapshot.get("installed", 0)) == 1
	var powered := int(snapshot.get("powered", 0)) == 1
	var workers := int(snapshot.get("workers", 0))
	var energy := int(snapshot.get("energy", 0))
	var tonne := int(snapshot.get("ore_tonnes", 0))
	var ready := int(snapshot.get("ready_tick", 0))
	var tick := int(snapshot.get("tick", 0))
	var text := "Station #%d | %d t ore | %d kg minerals\n" % [
		int(snapshot.get("station", 0)), tonne, int(snapshot.get("mineral_total_kg", 0))]
	if not installed:
		text += "NO INSTALLED REFINERY: construct one from finite ship modules"
	else:
		text += "Installed | workers %d | %s | energy %d | %s" % [
			workers, "powered" if powered else "OFFLINE", energy,
			"ready" if tick >= ready else "cooldown through tick %d" % ready]
	if int(snapshot.get("result", 0)) == 1:
		text += "\nNative commit: %d kg refined into station storage" % int(snapshot.get("transferred_kg", 0))
	elif int(snapshot.get("result", 0)) == 2:
		text += "\nOriginal C++ refused the last refining command"
	elif int(snapshot.get("result", 0)) == 3:
		text += "\nNative commit: %d physical tonne moved into station storage" % int(snapshot.get("unloaded_tonnes", 0))
	elif int(snapshot.get("result", 0)) == 4:
		text += "\nOriginal C++ refused the last unloading command"
	_status.text = text
	_lots.clear()
	var taxonomy_names := ["carbonaceous", "silicate", "metallic"]
	for lot in snapshot.get("lots", []):
		var taxonomy := int(lot.get("taxonomy", -1))
		if taxonomy < 0 or taxonomy > 2:
			continue
		var label := "Lot #%d | rock #%d | %d t | %s" % [
			int(lot["id"]), int(lot["rock"]), int(lot["tonnes"]),
			taxonomy_names[taxonomy]]
		_lots.add_item(label)
		_lots.set_item_metadata(_lots.item_count - 1, int(lot["id"]))
	var products := snapshot.get("minerals", {})
	var names := products.keys()
	names.sort()
	var rows := PackedStringArray()
	for product in names:
		rows.append("%s: %d kg" % [str(product).replace("_", " "), int(products[product])])
	_minerals.text = "Station stock: " + (", ".join(rows) if rows.size() > 0 else "none")
	_update_refine_button(0)
	_unload_button.disabled = int(snapshot.get("eligible_ship", 0)) <= 0
	if not installed or not powered or workers <= 0 or energy <= 0 or tonne < 1:
		_refine_button.disabled = true

func _update_refine_button(_ignored: int) -> void:
	if not is_instance_valid(_refine_button) or not is_instance_valid(_lots):
		return
	var tick := int(_current.get("tick", 0))
	var ready := int(_current.get("ready_tick", 0))
	_refine_button.disabled = not visible or _lots.get_selected_items().is_empty() or int(_current.get("installed", 0)) != 1 or int(_current.get("powered", 0)) != 1 or int(_current.get("workers", 0)) <= 0 or int(_current.get("energy", 0)) <= 0 or int(_current.get("ore_tonnes", 0)) <= 0 or tick < ready

func _on_refine_pressed() -> void:
	if not is_instance_valid(_refine_button) or _refine_button.disabled:
		return
	var indexes := _lots.get_selected_items()
	if indexes.is_empty():
		return
	var source_lot := int(_lots.get_item_metadata(indexes[0]))
	if source_lot <= 0:
		return
	_refine_button.disabled = true
	_status.text = "Submitted original lot #%d; awaiting SQLite commit receipt..." % source_lot
	refine_requested.emit(source_lot)

func _on_unload_pressed() -> void:
	if not is_instance_valid(_unload_button) or _unload_button.disabled:
		return
	# Do not change displayed inventory until a new original REFEND
	# receipt and source-verified lot snapshot arrive from SQLite.
	_unload_button.disabled = true
	_status.text = "Requesting one-tonne physical unload; awaiting original SQLite receipt..."
	unload_requested.emit()
