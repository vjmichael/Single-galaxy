extends PanelContainer
## PLAY-STATION-03C: strictly read-only display of the C++ station refinery.
## The ONLY emitted operation is the selected physical lot identifier.
signal refine_requested(lot_id: int)
signal unload_requested()
signal terminal_closed()
signal dock_status_requested()
signal dock_worker_requested()
signal dock_release_requested()
signal dock_freight_requested()
signal dock_passenger_requested()
signal dock_service_requested()
signal dock_destinations_requested()
signal dock_route_requested(destination: int, receiver: int)
signal dock_cancel_requested(route_id: int)

var _lots: ItemList
var _status: Label
var _dock_staff: Label
var _dock_status: Label
var _dock_assign_button: Button
var _dock_release_button: Button
var _dock_queue_button: Button
var _dock_serve_button: Button
var _destinations: OptionButton
var _route_button: Button
var _held_route_button: Button
var _reviewable_route := 0
var _minerals: Label
var _refine_button: Button
var _unload_button: Button
var _current := {}

func _ready() -> void:
	name = "ColonyInventoryAndOperations"
	position = Vector2(800, 110)
	custom_minimum_size = Vector2(450, 380)
	visible = false
	var column := VBoxContainer.new()
	add_child(column)
	var heading := Label.new()
	heading.text = "COLONY INVENTORY & DOCK OPERATIONS"
	heading.add_theme_font_size_override("font_size", 18)
	column.add_child(heading)
	_status = Label.new()
	_status.text = "No native refinery information"
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_status)
	_dock_staff = Label.new()
	_dock_staff.name = "OriginalDockWorkerRosterStatus"
	_dock_staff.text = "Dock workforce: checking local crew availability"
	_dock_staff.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_dock_staff)
	var docks_heading := Label.new()
	docks_heading.text = "DOCK SERVICES | STAFF & FREIGHT"
	column.add_child(docks_heading)
	_dock_status = Label.new()
	_dock_status.text = "Refresh to inspect this colony's actual dock queue"
	_dock_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_dock_status)
	var refresh := Button.new()
	refresh.text = "Refresh dock services"
	refresh.pressed.connect(func(): dock_status_requested.emit())
	column.add_child(refresh)
	var workers := HBoxContainer.new()
	column.add_child(workers)
	_dock_assign_button = Button.new()
	_dock_assign_button.text = "Assign available local worker"
	_dock_assign_button.pressed.connect(func(): dock_worker_requested.emit())
	workers.add_child(_dock_assign_button)
	_dock_release_button = Button.new()
	_dock_release_button.text = "End a dock shift"
	_dock_release_button.pressed.connect(func(): dock_release_requested.emit())
	workers.add_child(_dock_release_button)
	var passenger := Button.new()
	passenger.text = "Book a real ferry boarding"
	passenger.tooltip_text = "Requires two real owned ships at this colony, original named passenger, available ferry seats and supplies; boarding follows native dock-worker scheduling."
	passenger.pressed.connect(func(): dock_passenger_requested.emit())
	column.add_child(passenger)
	var freight := HBoxContainer.new()
	column.add_child(freight)
	_dock_queue_button = Button.new()
	_dock_queue_button.text = "Queue 1 tonne from docked ship"
	_dock_queue_button.disabled = true
	_dock_queue_button.pressed.connect(func(): dock_freight_requested.emit())
	freight.add_child(_dock_queue_button)
	_dock_serve_button = Button.new()
	_dock_serve_button.text = "Dispatch dock workers"
	_dock_serve_button.disabled = true
	_dock_serve_button.pressed.connect(func(): dock_service_requested.emit())
	freight.add_child(_dock_serve_button)
	var journeys_title := Label.new()
	journeys_title.text = "PASSENGER JOURNEYS | DESTINATION COLONIES"
	column.add_child(journeys_title)
	var journey_row := HBoxContainer.new()
	column.add_child(journey_row)
	var refresh_dest := Button.new()
	refresh_dest.text = "Find receiving colonies"
	refresh_dest.pressed.connect(func(): dock_destinations_requested.emit())
	journey_row.add_child(refresh_dest)
	_destinations = OptionButton.new()
	_destinations.custom_minimum_size = Vector2(190,0)
	_destinations.add_item("No destinations loaded")
	_destinations.disabled = true
	journey_row.add_child(_destinations)
	_route_button = Button.new()
	_route_button.text = "Send boarded ferry"
	_route_button.disabled = true
	_route_button.pressed.connect(_on_depart_pressed)
	journey_row.add_child(_route_button)
	_held_route_button = Button.new()
	_held_route_button.text = "Withdraw unstarted route"
	_held_route_button.disabled = true
	_held_route_button.pressed.connect(_on_review_pressed)
	column.add_child(_held_route_button)
	var lot_heading := Label.new()
	lot_heading.text = "This colony's ore lots | local owner hangar"
	column.add_child(lot_heading)
	_lots = ItemList.new()
	_lots.custom_minimum_size = Vector2(430, 115)
	_lots.item_selected.connect(_update_refine_button)
	column.add_child(_lots)
	_minerals = Label.new()
	_minerals.text = "Local colony minerals: waiting for original inventory"
	column.add_child(_minerals)
	_unload_button = Button.new()
	_unload_button.text = "Unload 1t from ship docked at THIS colony"
	_unload_button.tooltip_text = "Only this colony's native docked owned ship; original SQLite moves one real tonne"
	_unload_button.disabled = true
	_unload_button.pressed.connect(_on_unload_pressed)
	column.add_child(_unload_button)
	_refine_button = Button.new()
	_refine_button.text = "Refine selected one-tonne lot"
	_refine_button.disabled = true
	_refine_button.pressed.connect(_on_refine_pressed)
	column.add_child(_refine_button)
	var future := Label.new()
	future.text = "Manufacturing: not yet available. Build and staff real plants first.\nNo shared galactic inventory; goods remain at THIS colony."
	future.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(future)
	var close := Button.new()
	close.text = "Close colony interface (I)"
	close.pressed.connect(func(): terminal_closed.emit())
	column.add_child(close)

func set_dock_staff_observation(frame: Dictionary) -> void:
	# Counts are original C++ NPC presence/assignment projections, NOT
	# a second headcount or a command to recruit/teleport workers.
	if not is_instance_valid(_dock_staff):
		return
	var workers := int(frame.get("dock_staff",-1))
	var residents := int(frame.get("source_count",-1))
	if workers < 0 or residents < 0:
		_dock_staff.text = "Dock workers: station roster unavailable"
		return
	_dock_staff.text = "Dock: %d assigned workers; %d named residents observed%s" % [
		workers,residents," (bounded scan)" if bool(frame.get("scan_truncated",false)) else ""]
	if workers == 0:
		_dock_staff.text += " | No eligible dock worker is currently on shift"

func set_dock_status(snapshot: Dictionary) -> void:
	if not is_instance_valid(_dock_status):
		return
	var staff := int(snapshot.get("dock_staff",0))
	var passengers := int(snapshot.get("passengers",0))
	var tonnes := int(snapshot.get("freight_tonnes",0))
	var held := int(snapshot.get("held_jobs",0))
	_dock_status.text = "Station #%d | %d available dock workers | %d experienced cargo handlers\n%d services/cycle | %d passenger gangways | %d%% boarding congestion\n%d passengers awaiting real boarding (%d urgent) | %d completed ferry boardings\n%d t real docked freight | %d held freight | %d held passenger requests\n%d t unloaded to this colony's original hangar" % [
		int(snapshot.get("station",0)),staff,int(snapshot.get("qualified_staff",0)),
		int(snapshot.get("service_capacity",0)),int(snapshot.get("gangways",0)),
		int(snapshot.get("congestion",0)),int(snapshot.get("boarding_waiting",0)),
		int(snapshot.get("urgent_boarding",0)),int(snapshot.get("actually_boarded",0)),
		tonnes,held,int(snapshot.get("boarding_held",0)),
		int(snapshot.get("handled_tonnes",0))]
	_dock_status.text += "\nFerries: %d travelling | %d destination journeys completed | %d held" % [
		int(snapshot.get("active_routes",0)),
		int(snapshot.get("delivered_routes",0)),
		int(snapshot.get("held_routes",0))]
	_dock_status.text += "\nReal passengers aboard/in transit: %d | reached destination walkway: %d" % [
		int(snapshot.get("passengers_travelling",0)),
		int(snapshot.get("visitors_arrived",0))]
	_dock_staff.text = "Dock workers: %d on active original-world shifts" % staff
	_reviewable_route = int(snapshot.get("reviewable_route",0))
	_held_route_button.disabled = _reviewable_route<=0
	_route_button.disabled = _destinations.disabled or \
		int(snapshot.get("actually_boarded",0))<=0
	_dock_serve_button.disabled = (staff <= 0 and int(snapshot.get("active_routes",0))<=0) or \
		(passengers+tonnes+int(snapshot.get("boarding_waiting",0))+
		int(snapshot.get("active_routes",0)))==0
	_dock_release_button.disabled = staff <= 0
	_dock_queue_button.disabled = int(_current.get("eligible_ship",0)) <= 0

func set_destinations(frame: Dictionary) -> void:
	if not is_instance_valid(_destinations):
		return
	_destinations.clear()
	var choices: Array = frame.get("destinations",[])
	for option in choices:
		var port := int(option.get("station",0))
		var ship := int(option.get("receiver",0))
		if port<=0 or ship<=0:
			continue
		var label := "Colony #%d · Arrival ship #%d" % [port,ship]
		_destinations.add_item(label)
		_destinations.set_item_metadata(_destinations.item_count-1,
			{"station":port,"receiver":ship})
	_destinations.disabled = _destinations.item_count==0
	_route_button.disabled = _destinations.disabled

func _on_depart_pressed() -> void:
	if _route_button.disabled or _destinations.disabled:
		return
	var selected := _destinations.selected
	if selected<0 or selected>=_destinations.item_count:
		return
	var target: Dictionary = _destinations.get_item_metadata(selected)
	var port := int(target.get("station",0))
	var receiver := int(target.get("receiver",0))
	if port>0 and receiver>0:
		_route_button.disabled = true
		dock_route_requested.emit(port,receiver)

func _on_review_pressed() -> void:
	if _held_route_button.disabled or _reviewable_route<=0:
		return
	_held_route_button.disabled=true
	dock_cancel_requested.emit(_reviewable_route)

func show_waiting() -> void:
	visible = true
	_current.clear()
	if is_instance_valid(_lots):
		_lots.clear()
	if is_instance_valid(_status):
		_status.text = "Contacting colony inventory..."
	if is_instance_valid(_dock_status):
		_dock_status.text = "Refresh dock services to inspect saved shifts and queues"
	if is_instance_valid(_dock_serve_button):
		_dock_serve_button.disabled = true
	if is_instance_valid(_dock_queue_button):
		_dock_queue_button.disabled = true
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
	var text := "COLONY #%d | PRIVATE LOCAL INVENTORY\nOre %d t | minerals %d kg\n" % [
		int(snapshot.get("station", 0)), tonne, int(snapshot.get("mineral_total_kg", 0))]
	if not installed:
		text += "REFINERY NOT BUILT — construction and staffing required"
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
	var products: Dictionary = snapshot.get("minerals", {})
	var names: Array = products.keys()
	names.sort()
	var rows := PackedStringArray()
	for product in names:
		rows.append("%s: %d kg" % [str(product).replace("_", " "), int(products[product])])
	_minerals.text = "This colony's mineral stock: " + (", ".join(rows) if rows.size() > 0 else "none")
	_update_refine_button(0)
	_unload_button.disabled = int(snapshot.get("eligible_ship", 0)) <= 0
	_dock_queue_button.disabled = int(snapshot.get("eligible_ship",0)) <= 0
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
	_status.text = "Original dock unloading 1t to THIS colony; awaiting SQLite receipt..."
	unload_requested.emit()
