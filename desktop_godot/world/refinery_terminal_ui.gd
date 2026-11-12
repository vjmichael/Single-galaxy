extends PanelContainer
## PLAY-STATION-03C: strictly read-only display of the C++ station refinery.
## The ONLY emitted operation is the selected physical lot identifier.
signal refine_requested(lot_id: int)
signal unload_requested()
signal industry_refresh_requested()
signal industry_batch_requested(trade: int)
signal industry_tick_requested()
signal industry_run_requested()
signal industry_transfer_requested(selector: int)
signal industry_management_refresh_requested()
signal industry_management_apply_requested(selector: int)
signal terminal_closed()
signal dock_status_requested()
signal dock_worker_requested()
signal dock_release_requested()
signal dock_renew_requested()
signal dock_freight_requested()
signal dock_passenger_requested()
signal dock_service_requested()
signal dock_destinations_requested()
signal dock_route_requested(destination: int, receiver: int)
signal dock_cancel_requested(route_id: int)
signal dock_region_source_requested(index: int)
signal dock_region_bind_requested(index: int, authority_generation: int,
	checkpoint_revision: int, rate_per_million: int)

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
var _region_index := 0
var _region_total := 0
var _region_generation := 0
var _region_revision := 0
var _region_description: Label
var _region_rate: OptionButton
var _region_link: Button
var _region_next: Button
var _minerals: Label
var _refine_button: Button
var _unload_button: Button
var _current := {}
var _industry_state := {}
var _industry_status: Label
var _industry_stocks: ItemList
var _industry_jobs: Label
var _industry_cut_button: Button
var _industry_shape_button: Button
var _industry_tick_button: Button
var _industry_run_button: Button
var _industry_transfers: Array[Button] = []
var _management_status: Label
var _management_workers: Label
var _management_action: OptionButton
var _management_apply: Button
var _management_snapshot := {}
var _industrial_next_step: Label

func _ready() -> void:
	name = "ColonyInventoryAndOperations"
	position = Vector2(800, 110)
	custom_minimum_size = Vector2(450, 380)
	visible = false
	var scroll := ScrollContainer.new()
	scroll.custom_minimum_size = Vector2(535, 620)
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(scroll)
	var column := VBoxContainer.new()
	scroll.add_child(column)
	var heading := Label.new()
	heading.text = "COLONY INVENTORY & DOCK OPERATIONS"
	heading.add_theme_font_size_override("font_size", 18)
	column.add_child(heading)
	_industrial_next_step=Label.new()
	_industrial_next_step.text="DOCK-09 | Build and reach an original outpost to begin industrial production"
	_industrial_next_step.autowrap_mode=TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_industrial_next_step)
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
	var extend_shift := Button.new()
	extend_shift.text = "Extend active shift"
	extend_shift.tooltip_text = "One human-authorized bounded extension of existing real shore crew duty"
	extend_shift.pressed.connect(func(): dock_renew_requested.emit())
	workers.add_child(extend_shift)
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
	var regional_title := Label.new()
	regional_title.text = "REGIONAL POPULATION | LINK ORIGINAL SOURCE"
	column.add_child(regional_title)
	var population_row := HBoxContainer.new()
	column.add_child(population_row)
	_region_description = Label.new()
	_region_description.text = "No regional population linked"
	_region_description.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_region_description)
	var find_region := Button.new()
	find_region.text = "Find population source"
	find_region.pressed.connect(func(): dock_region_source_requested.emit(1))
	population_row.add_child(find_region)
	_region_next = Button.new()
	_region_next.text = "Next source"
	_region_next.disabled = true
	_region_next.pressed.connect(_on_next_region)
	population_row.add_child(_region_next)
	_region_rate = OptionButton.new()
	_region_rate.add_item("No travel forecast")
	_region_rate.set_item_metadata(0,0)
	for number in [1,10,100,1000]:
		_region_rate.add_item("%d potential travelers per million" % number)
		_region_rate.set_item_metadata(_region_rate.item_count-1,number)
	column.add_child(_region_rate)
	_region_link = Button.new()
	_region_link.text = "Link selected population to THIS colony"
	_region_link.disabled = true
	_region_link.pressed.connect(_on_link_region)
	column.add_child(_region_link)
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
	var industry_heading := Label.new()
	industry_heading.text = "LOCAL FACTORY / WAREHOUSE / CONSTRUCTION GOODS"
	column.add_child(industry_heading)
	_industry_status = Label.new()
	_industry_status.text = "Factory and storage inventory awaiting original C++ source"
	_industry_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_industry_status)
	var refresh_factory := Button.new()
	refresh_factory.text = "Refresh this colony's industrial stores"
	refresh_factory.pressed.connect(func(): industry_refresh_requested.emit())
	column.add_child(refresh_factory)
	_industry_stocks = ItemList.new()
	_industry_stocks.custom_minimum_size = Vector2(470, 150)
	# Native rows are read-only; item clicks do not grant transfer rights.
	column.add_child(_industry_stocks)
	_industry_jobs = Label.new()
	_industry_jobs.text = "Native production jobs: no verified local workshop"
	_industry_jobs.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_industry_jobs)
	var job_buttons := HBoxContainer.new()
	column.add_child(job_buttons)
	_industry_cut_button = Button.new()
	_industry_cut_button.text = "Approve 1 panel batch"
	_industry_cut_button.disabled = true
	_industry_cut_button.tooltip_text = "Original approved workshop recipe only: 20 kg physical aluminum to 19 kg panels and 1 kg scrap"
	_industry_cut_button.pressed.connect(func(): industry_batch_requested.emit(1))
	job_buttons.add_child(_industry_cut_button)
	_industry_shape_button = Button.new()
	_industry_shape_button.text = "Approve 1 frame batch"
	_industry_shape_button.disabled = true
	_industry_shape_button.tooltip_text = "Original approved workshop recipe only: 20 kg physical panels to 19 kg frames and 1 kg scrap"
	_industry_shape_button.pressed.connect(func(): industry_batch_requested.emit(2))
	job_buttons.add_child(_industry_shape_button)
	var transfer_labels := [
		"Move 20 kg aluminum: docked ship → warehouse",
		"Move 20 kg aluminum: warehouse → workshop",
		"Move 19 kg panels: workshop → warehouse",
		"Move 19 kg frames: workshop → warehouse",
		"Move 1 kg scrap: workshop → warehouse"]
	for index in range(transfer_labels.size()):
		var transfer := Button.new()
		transfer.text = transfer_labels[index]
		transfer.disabled = true
		transfer.pressed.connect(_on_industry_transfer_pressed.bind(index+1))
		column.add_child(transfer)
		_industry_transfers.append(transfer)
	_industry_tick_button = Button.new()
	_industry_tick_button.text = "Advance original production tick"
	_industry_tick_button.disabled = true
	_industry_tick_button.tooltip_text = "Runs one native simulation tick on this save; never grants material or instant completed goods"
	_industry_tick_button.pressed.connect(func(): industry_tick_requested.emit())
	column.add_child(_industry_tick_button)
	_industry_run_button = Button.new()
	_industry_run_button.text = "Run 16 original workshop ticks (bounded)"
	_industry_run_button.disabled = true
	_industry_run_button.tooltip_text = "Commits 16 real original-world steps; only verified physical batches can add inventory. No simulation of output in Godot."
	_industry_run_button.pressed.connect(func(): industry_run_requested.emit())
	column.add_child(_industry_run_button)
	var manager_title := Label.new()
	manager_title.text = "INDUSTRIAL FACILITIES | NAMED WORKERS | ORIGINAL SHIFTS"
	column.add_child(manager_title)
	_management_status = Label.new()
	_management_status.text = "Inspect actual colony facilities, material modules and utilities."
	_management_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_management_status)
	_management_workers = Label.new()
	_management_workers.text = "No original industrial staffing report yet"
	_management_workers.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_management_workers)
	var manager_refresh := Button.new()
	manager_refresh.text = "Refresh facility and workforce status"
	manager_refresh.pressed.connect(func(): industry_management_refresh_requested.emit())
	column.add_child(manager_refresh)
	_management_action = OptionButton.new()
	_management_action.custom_minimum_size = Vector2(490,0)
	_management_action.add_item("Choose one native facility / workforce command")
	_management_action.set_item_disabled(0,true)
	var labels := [
		"Build workshop — original construction module",
		"Build warehouse — original construction module",
		"Build local utility plant — original construction modules",
		"Build local habitation — original construction modules",
		"Install workshop frame-finishing machine",
		"Build and commission physical station refinery",
		"Deliver 100 kg ship food to local habitation",
		"Deliver 100 kg ship water to local habitation",
		"Deliver 100 kg ship oxygen to local habitation",
		"Move one real resident from ship into habitation",
		"Appoint one existing resident as utility technician",
		"Deliver one existing physical utility spare",
		"Authorize maintained-utility service",
		"Repair original installed utility with real modules",
		"Train one available local panel trade worker (1 kg)",
		"Train one available local frame trade worker (1 kg)",
		"Appoint certified original panel supervisor",
		"Appoint certified original frame supervisor",
		"Schedule 16-tick original panel-operator shift",
		"Schedule 16-tick original frame-finisher shift",
		"Assign real available panel workshop operator",
		"Assign real available frame workshop finisher",
		"Cancel original approved panel batch program",
		"Cancel original approved frame batch program",
		"Release original panel operator from workshop",
		"Release original frame finisher from workshop",
		"Cancel original panel-operator shift",
		"Cancel original frame-finisher shift"]
	for index in range(labels.size()):
		_management_action.add_item(labels[index])
		_management_action.set_item_metadata(index+1,index+1)
		_management_action.set_item_disabled(index+1,true)
	_management_action.select(0)
	_management_action.item_selected.connect(func(_index: int): _update_management_apply())
	column.add_child(_management_action)
	_management_apply = Button.new()
	_management_apply.text = "Authorize one selected original-world action"
	_management_apply.disabled = true
	_management_apply.tooltip_text = "Native C++ rechecks actual colony ownership, physical ship, material, workforce, facility, energy and exact journal outcome."
	_management_apply.pressed.connect(_on_management_apply)
	column.add_child(_management_apply)
	var future := Label.new()
	future.text = "Only existing native workshop panel/frame recipes are executable. Candidate recipes, station-mineral feed conversion, unattended factories and intercolony stock transfer are not authorized."
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
	if is_instance_valid(_region_description) and bool(snapshot.get("regional_source_bound",false)):
		_region_description.text = "Verified population: %d | capacity-limited travel interest: %d" % [
			int(snapshot.get("cold_population",0)),
			int(snapshot.get("travel_pressure",0))]
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

func set_regional_source(frame: Dictionary) -> void:
	if not is_instance_valid(_region_description):
		return
	_region_total=int(frame.get("count",0))
	_region_index=int(frame.get("index",0))
	_region_generation=int(frame.get("authority_generation",0))
	_region_revision=int(frame.get("revision",0))
	_region_next.disabled = _region_index<=0 or _region_index>=_region_total
	_region_link.disabled = _region_index<=0 or \
		_region_generation<=0 or _region_revision<=0
	if _region_index<=0:
		_region_description.text = "No unambiguous cold-region population source"
		return
	_region_description.text = "Source %d of %d | %s / %s | %d verified aggregate residents" % [
		_region_index,_region_total,str(frame.get("scope","")),
		str(frame.get("colony","")),int(frame.get("population",0))]

func _on_next_region() -> void:
	if _region_index>0 and _region_index<_region_total:
		dock_region_source_requested.emit(_region_index+1)

func _on_link_region() -> void:
	if _region_link.disabled or _region_index<=0 or \
			_region_rate.selected<0:
		return
	var rate := int(_region_rate.get_item_metadata(_region_rate.selected))
	_region_link.disabled = true
	dock_region_bind_requested.emit(_region_index,_region_generation,
		_region_revision,rate)

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
	_industry_state.clear()
	_management_snapshot.clear()
	if is_instance_valid(_industrial_next_step):
		_industrial_next_step.text="DOCK-09 | Inspecting original source colony and physical factory..."
	if is_instance_valid(_management_status):
		_management_status.text="Inspecting original local factory facilities and named workers..."
	if is_instance_valid(_management_workers):
		_management_workers.text="Native supervisor/shift status pending"
	if is_instance_valid(_management_action):
		_management_action.select(0)
		for index in range(1,_management_action.item_count):
			_management_action.set_item_disabled(index,true)
	if is_instance_valid(_management_apply):
		_management_apply.disabled=true
	if is_instance_valid(_industry_stocks):
		_industry_stocks.clear()
	if is_instance_valid(_industry_status):
		_industry_status.text = "Contacting original colony-local factory inventory..."
	if is_instance_valid(_industry_jobs):
		_industry_jobs.text = "Original workforce and production status pending"
	if is_instance_valid(_industry_cut_button):
		_industry_cut_button.disabled=true
	if is_instance_valid(_industry_shape_button):
		_industry_shape_button.disabled=true
	if is_instance_valid(_industry_tick_button):
		_industry_tick_button.disabled=true
	if is_instance_valid(_industry_run_button):
		_industry_run_button.disabled=true
	for transfer in _industry_transfers:
		transfer.disabled=true
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
	if is_instance_valid(_management_status):
		_management_status.text = message
	if is_instance_valid(_management_apply):
		_management_apply.disabled=true
	if is_instance_valid(_status):
		_status.text = message
	if is_instance_valid(_refine_button):
		_refine_button.disabled = true
	if is_instance_valid(_unload_button):
		_unload_button.disabled = true

func _on_industry_transfer_pressed(selector: int) -> void:
	if selector<1 or selector>_industry_transfers.size():
		return
	var button := _industry_transfers[selector-1]
	if button.disabled:
		return
	button.disabled = true
	industry_transfer_requested.emit(selector)

func _update_industrial_next_step() -> void:
	if not is_instance_valid(_industrial_next_step):
		return
	var ind: Dictionary = _industry_state
	var mgmt: Dictionary = _management_snapshot
	if ind.is_empty():
		_industrial_next_step.text="DOCK-09 | Inspecting real colony and existing source stocks..."
		return
	var next := ""
	if not bool(ind.get("constructed",false)):
		next="This is a starter station, not a built factory. Board the real colony ship, mine finite ore, refine it and form at least four modules. In space, stop clear of the starter dock, build your outpost, dock there and reopen I."
	elif mgmt.is_empty() or int(mgmt.get("station",-1))!=int(ind.get("station",-2)):
		next="Built outpost found. Inspect original local facilities and staff before authorizing any work."
	elif not bool(mgmt.get("workshop",false)):
		next="Select 'Build workshop' in the facility menu; it consumes an original construction module."
	elif not bool(mgmt.get("storage",false)):
		next="Construct the local warehouse so ship goods can physically reach the workshop."
	elif not bool(mgmt.get("utility",false)):
		next="Construct local utility service from two original modules; the workshop cannot run without native power."
	elif not bool(mgmt.get("habitat",false)):
		next="Construct habitat from two original modules; factory labor must be housed locally."
	elif int(mgmt.get("residents",0))<=0:
		next="Transfer actual food, water and oxygen to the habitat, then assign an existing ship resident; a factory does not spawn workers."
	else:
		var found_aluminum := 0
		var waiting_aluminum := 0
		var panels_kg := 0
		var stocked_kg := 0
		for row in ind.get("stocks",[]):
			if row.get("kind","")=="H" and row.get("code","")=="good/aluminum":
				found_aluminum=int(row.get("kg",0))
			elif row.get("kind","")=="S" and int(row.get("bin",0))==0 and row.get("code","")=="good/aluminum":
				waiting_aluminum=int(row.get("kg",0))
			elif row.get("kind","")=="W" and row.get("code","")=="good/aluminum":
				stocked_kg=int(row.get("kg",0))
			elif row.get("kind","")=="W" and row.get("code","")=="good/aluminum_panels":
				panels_kg=int(row.get("kg",0))
		var cut_job := {}
		for job in ind.get("jobs",[]):
			if int(job.get("trade",0))==1:
				cut_job=job
		if panels_kg>=19 and not cut_job.is_empty() and \
				int(cut_job.get("completed",0))>=1:
			next="A real panel batch is complete. Physically transfer 19 kg panels to the local warehouse and verify its stock row."
		elif not cut_job.is_empty() and bool(cut_job.get("approved",false)) and \
				int(cut_job.get("completed",0))<int(cut_job.get("target",0)):
			next="One original panel batch is approved. Run the bounded 16 native ticks; a result without physical output is explicitly marked stalled."
		elif int(mgmt.get("supervisor",0))==0 and stocked_kg<3:
			next="Load aluminum into the workshop from the docked ship via warehouse; local cut-trade training consumes real aluminum."
		elif int(mgmt.get("supervisor",0))==0:
			next="Train an eligible actual local cut-trade worker three times, then appoint a certified panel supervisor."
		elif not cut_job.is_empty() and int(cut_job.get("worker",0))==0:
			next="Schedule a native 16-tick panel-operator shift, or assign an eligible original resident; supervisor and operator must be distinct."
		elif stocked_kg<20 and found_aluminum<20 and waiting_aluminum<20:
			next="No physical workshop feed exists here; mine original geological silicates in space and separate their plagioclase in your colony ship."
		elif stocked_kg<20:
			next="Replenish 20 kg original aluminum via ship → warehouse → workshop. Training has consumed real feed."
		else:
			next="Approve one original 20 kg aluminum → 19 kg panels batch, then run bounded native ticks and verify source output. If approval is disabled, inspect native shift, power and supervisor gates."
	_industrial_next_step.text="DOCK-09 | NEXT ORIGINAL STEP: "+next

func _update_management_apply() -> void:
	if not is_instance_valid(_management_apply) or \
			not is_instance_valid(_management_action):
		return
	var selected := _management_action.selected
	if selected<=0:
		_management_apply.disabled=true
		return
	var selector := int(_management_action.get_item_metadata(selected))
	var options: Dictionary = _management_snapshot.get("options",{})
	_management_apply.disabled=not bool(options.get(selector,false))

func _on_management_apply() -> void:
	if _management_apply.disabled or _management_action.selected<=0:
		return
	var selector := int(_management_action.get_item_metadata(
		_management_action.selected))
	var options: Dictionary = _management_snapshot.get("options",{})
	if selector<1 or selector>28 or not bool(options.get(selector,false)):
		return
	_management_apply.disabled=true
	_management_status.text = "Authorizing one real original-world task; waiting for native SQLite event..."
	industry_management_apply_requested.emit(selector)

func set_industry_management(snapshot: Dictionary) -> void:
	# Original physical facilities and distinct named NPC labor are the only
	# authority. This panel neither books people nor constructs equipment.
	if not is_instance_valid(_management_status):
		return
	_management_snapshot=snapshot.duplicate(true)
	_update_industrial_next_step()
	var msg := "Colony #%d | founding hull #%d %s\n" % [
		int(snapshot.get("station",0)),int(snapshot.get("source_ship",0)),
		"docked here" if bool(snapshot.get("docked",false)) else "not docked here"]
	msg += "Original facilities: workshop %s, warehouse %s, utility %s, habitation %s, finisher %s, refinery %s\n" % [
		"built" if snapshot.get("workshop",false) else "not built",
		"built" if snapshot.get("storage",false) else "not built",
		"built" if snapshot.get("utility",false) else "not built",
		"built" if snapshot.get("habitat",false) else "not built",
		"installed" if snapshot.get("finisher",false) else "not installed",
		"built" if snapshot.get("refinery",false) else "not built"]
	msg += "Physical construction modules: %d | residents: %d | utility technicians: %d\n" % [
		int(snapshot.get("modules",0)),int(snapshot.get("residents",0)),
		int(snapshot.get("technicians",0))]
	msg += "Utility: %s, stored energy %d, spares %d | named supervisor #%d (trade %d)" % [
		"powered" if snapshot.get("powered",false) else "offline",
		int(snapshot.get("energy",0)),int(snapshot.get("spares",0)),
		int(snapshot.get("supervisor",0)),int(snapshot.get("supervisor_trade",0))]
	if int(snapshot.get("result",0))==1:
		msg += "\nCommitted action #%d to the ORIGINAL world journal." % int(snapshot.get("chosen",0))
	elif int(snapshot.get("result",0))==2:
		msg += "\nOriginal C++ rejected action #%d; no success credited." % int(snapshot.get("chosen",0))
	_management_status.text=msg
	var worker_rows := PackedStringArray()
	var labor_gates := [
		"available", "owner mismatch", "no physical workshop",
		"foreign site", "no real habitation", "finisher not installed",
		"already staffed", "no free qualified resident"]
	var work_states := [
		"physical batch in progress", "ready for native production",
		"staff unavailable", "power unavailable", "maintenance required",
		"physical input short", "output warehouse full",
		"machine not installed", "approved batch quota complete",
		"awaiting certified supervisor"]
	for row in snapshot.get("workers",[]):
		var gate := int(row.get("gate",0))
		var state := int(row.get("state",0))
		var end_tick := int(row.get("shift_end",0))
		worker_rows.append("%s | worker #%d, candidate #%d | trained %d/3 | %s | %s | %s" % [
			"Panel machine" if int(row.get("trade",0))==1 else "Frame machine",
			int(row.get("worker",0)),int(row.get("suggested",0)),
			int(row.get("training_units",0)),
			"shift ends tick %d" % end_tick if end_tick>0 else "no timed shift",
			labor_gates[gate] if gate>=0 and gate<labor_gates.size() else "invalid gate",
			work_states[state] if state>=0 and state<work_states.size() else "invalid state"])
	_management_workers.text = "\n".join(worker_rows) if worker_rows.size()>0 else \
		"No constructed original workshop; no phantom employees or work shifts"
	var options: Dictionary = snapshot.get("options",{})
	for index in range(1,_management_action.item_count):
		var selector := int(_management_action.get_item_metadata(index))
		_management_action.set_item_disabled(index,not bool(options.get(selector,false)))
	_management_action.select(0)
	_update_management_apply()

func set_industry(snapshot: Dictionary) -> void:
	# Never merge warehouse, workshop, refinery or escrow stock into a second
	# account balance. Every amount is a native source-tagged observation.
	if not is_instance_valid(_industry_status):
		return
	_industry_state = snapshot.duplicate(true)
	_update_industrial_next_step()
	var constructed := bool(snapshot.get("constructed",false))
	var plant := bool(snapshot.get("refinery",false))
	var workshop := bool(snapshot.get("workshop",false))
	var store := bool(snapshot.get("storage",false))
	var station := int(snapshot.get("station",0))
	var escrow := int(snapshot.get("escrow_kg",0))
	var result := int(snapshot.get("result",0))
	_industry_status.text = "Colony #%d | constructed site %s | refinery %s | workshop %s | storage %s\nOwner hangar ore: %d t | reserved original factory-network escrow: %d kg%s" % [
		station,"yes" if constructed else "no","yes" if plant else "no",
		"yes" if workshop else "no","yes" if store else "no",
		int(snapshot.get("hangar_ore_t",0)),escrow,
		"\nOnly the original workshop job approval was committed." if result==1 else
		"\nOriginal workshop declined that approval." if result==2 else
		"\nOne native production tick completed; see actual stock and batches." if result==3 else
		"\nOriginal world committed a physical local stock transfer." if result==4 else
		"\nOriginal world refused this stock transfer." if result==5 else
		"\nPhysical original workshop batch completed during 16 native steps." if result==6 else
		"\nSixteen genuine native steps; no physical batch completed (inspect staff, shift, material and power)." if result==7 else ""]
	_industry_stocks.clear()
	var available := {}
	for stock in snapshot.get("stocks",[]):
		var kind := str(stock.get("kind",""))
		var code := str(stock.get("code",""))
		var source := "Refinery concentrate" if kind=="R" else \
			"Workshop buffer" if kind=="W" else \
			"Docked founding ship" if kind=="H" else \
			"Original hull modules" if kind=="C" else \
			"Warehouse bin #%d" % int(stock.get("bin",0))
		var units := "items" if kind=="C" else "kg"
		_industry_stocks.add_item("%s | %s | %d %s" % [
			source,code,int(stock.get("kg",0)),units])
		var stock_key := "%s/%d/%s" % [kind,int(stock.get("bin",0)),code]
		available[stock_key]=int(stock.get("kg",0))
	if _industry_stocks.item_count==0:
		_industry_stocks.add_item("No original industrial stock at this colony")
	if bool(snapshot.get("truncated",false)):
		_industry_stocks.add_item("Source view truncated at 64 stock rows — refresh after inspecting the native site")
	var descriptions := PackedStringArray()
	var cut_ready := false
	var shape_ready := false
	var active := false
	for job in snapshot.get("jobs",[]):
		var trade := int(job.get("trade",0))
		var approved := bool(job.get("approved",false))
		var has_staff := int(job.get("worker",0))>0 and int(job.get("supervisor",0))>0
		var powered := bool(job.get("powered",false))
		var on_shift := bool(job.get("on_shift",false))
		var material := int(job.get("input",0))
		var job_ready := workshop and not approved and has_staff and powered and on_shift and material>=20
		if trade==1:
			cut_ready=job_ready
		elif trade==2:
			shape_ready=job_ready
		active = active or (approved and int(job.get("completed",0)) < int(job.get("target",0)))
		descriptions.append("%s: input %d kg, WIP %d kg, output %d kg; labor %s; power %s; program %s" % [
			"Panels" if trade==1 else "Frames",
			material,int(job.get("reserved",0)),int(job.get("output",0)),
			"ready" if has_staff and on_shift else "unavailable",
			"on" if powered else "off",
			"%d / %d batches" % [int(job.get("completed",0)),
				int(job.get("target",0))] if approved else "not approved"])
	_industry_jobs.text = "\n".join(descriptions) if descriptions.size()>0 else \
		"No constructed, staffed original workshop at this colony"
	_industry_cut_button.disabled=not cut_ready
	_industry_shape_button.disabled=not shape_ready
	_industry_tick_button.disabled=not active
	_industry_run_button.disabled=not active
	var transfer_requirements := [
		"H/0/good/aluminum",
		"S/0/good/aluminum",
		"W/0/good/aluminum_panels",
		"W/0/good/aluminum_frames",
		"W/0/good/aluminum_scrap"]
	var quantities := [20,20,19,19,1]
	for index in range(_industry_transfers.size()):
		var enough := int(available.get(transfer_requirements[index],0))>=quantities[index]
		_industry_transfers[index].disabled=not (store and workshop and enough)

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
