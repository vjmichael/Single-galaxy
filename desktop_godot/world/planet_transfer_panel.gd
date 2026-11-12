extends PanelContainer
## S6-F5-F human-facing presentation of ORIGINAL TFRAME/TSITE only.
## Each click issues one proposal; C++/SQLite owns every state transition.
signal action_requested(action: String)
signal site_requested(index: int)
signal return_to_cockpit_requested()
var _title: Label
var _status: Label
var _sites: OptionButton
var _actions: Dictionary = {}
var _cancel: Button
var _refreshing := false
var _last_site_key := ""

func _ready() -> void:
    name="OriginalShuttleTransferPanel_NOT_AUTHORITY"
    position=Vector2(540,110)
    custom_minimum_size=Vector2(410,0)
    var style := StyleBoxFlat.new()
    style.bg_color=Color(0.06,0.09,0.12,0.95)
    style.set_content_margin_all(16)
    style.set_corner_radius_all(6)
    add_theme_stylebox_override("panel",style)
    var content := VBoxContainer.new()
    content.add_theme_constant_override("separation",9)
    add_child(content)
    _title=Label.new()
    _title.text="ORIGINAL PLANETARY SHUTTLE"
    content.add_child(_title)
    _status=Label.new()
    _status.autowrap_mode=TextServer.AUTOWRAP_WORD_SMART
    _status.custom_minimum_size.y=92
    content.add_child(_status)
    _sites=OptionButton.new()
    _sites.item_selected.connect(_on_site_selected)
    content.add_child(_sites)
    for pair in [
        ["BOARD","Board original shuttle"],
        ["DESCEND","Launch physical descent"],
        ["DISEMBARK","Disembark at source site"],
        ["ASCEND","Launch physical ascent"],
        ["EXIT","Exit to original ship"]]:
        var action: String=pair[0]
        var button := Button.new()
        button.text=pair[1]
        button.pressed.connect(_on_action_pressed.bind(action))
        content.add_child(button)
        _actions[action]=button
    _cancel=Button.new()
    _cancel.text="Return to ship controls (without boarding)"
    _cancel.pressed.connect(func(): return_to_cockpit_requested.emit())
    content.add_child(_cancel)
    visible=false

func _on_action_pressed(action: String) -> void:
    action_requested.emit(action)

func _on_site_selected(index: int) -> void:
    if not _refreshing:
        site_requested.emit(index)

func present(frame: Dictionary, options: Array[Dictionary], pending: bool=false) -> void:
    visible=true
    if frame.is_empty() or options.is_empty():
        _status.text="Original transfer verification pending; no operation authorized."
        for button in _actions.values():
            button.disabled=true
        _cancel.disabled=true
        _sites.disabled=true
        return
    var state := int(frame["state"])
    var phase := int(frame["phase"])
    var key := JSON.stringify(options)
    if key!=_last_site_key:
        _refreshing=true
        _sites.clear()
        for o in options:
            _sites.add_item("Body %d / latitude %d / longitude %d / original site %d" % [
                int(o["body"]),int(o["lat"]),int(o["lon"]),int(o["site"])])
        _refreshing=false
        _last_site_key=key
    _refreshing=true
    if _sites.selected!=int(frame["selected"]):
        _sites.select(int(frame["selected"]))
    _refreshing=false
    _sites.disabled=pending or state!=0 or options.size()<2
    var person_location := "ORIGINAL SHIP" if state==0 else (
        "IN NATIVE SHUTTLE" if state==5 else "IN NATIVE TRANSIT")
    var shuttle_phase := ["ORBIT","DESCENDING","LANDED","ASCENDING"][phase]
    _status.text="%s | shuttle %s\nWorld tick %d | SQLite generation %d\nOriginal body %d, site %d\nEvery action requires an independent committed native receipt." % [
        person_location,shuttle_phase,int(frame["tick"]),int(frame["gen"]),
        int(frame["body"]),int(frame["site"])]
    for action in _actions.keys():
        _actions[action].disabled=true
    if not pending:
        if state==0 and phase==0:
            _actions["BOARD"].disabled=false
        elif state==5 and phase==0:
            _actions["DESCEND"].disabled=false
            _actions["EXIT"].disabled=false # cancel boarding, or completed return
        elif state==5 and phase==2:
            # A landed shuttle may be disembarked or flown back. Never infer
            # a physical action merely from a visual landing marker.
            _actions["DISEMBARK"].disabled=false
            _actions["ASCEND"].disabled=false
    _cancel.disabled=pending or state!=0

func report(message: String) -> void:
    if visible:
        _status.text=message.left(250)
