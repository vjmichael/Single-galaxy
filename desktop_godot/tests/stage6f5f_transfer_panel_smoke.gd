extends SceneTree
## Synthetic Godot admission-only smoke. No OS process and no real SQLite
## mutation; full flight/recovery is the separate original native CTest.
const BRIDGE := preload("res://world/native_planet_transfer_session.gd")
const PANEL := preload("res://world/planet_transfer_panel.gd")

func _initialize() -> void:
    call_deferred("_run")

func _run() -> void:
    var bridge=BRIDGE.new()
    bridge._active=true # test-only packet ingress; never launch a writer
    bridge._pid=-1
    bridge._accept("TSITE|1|77|0|123|17|36|99|0")
    bridge._accept("TFRAME|1|77|0|3|0|123|17|36|0|5|8|99|0|0|0|0|0|1")
    assert(bridge.has_verified_state())
    assert(bridge.current_state()["state"]==0)
    assert(bridge.sites().size()==1)
    var panel=PANEL.new()
    get_root().add_child(panel)
    panel.present(bridge.current_state(),bridge.sites())
    assert(panel.visible)
    assert(not panel._actions["BOARD"].disabled)
    assert(panel._actions["DESCEND"].disabled)
    bridge._pending=1
    bridge._pending_action="BOARD"
    bridge._accept("TFRAME|1|77|1|3|5|123|17|36|0|6|9|99|0|0|0|777|0|1")
    assert(bridge.has_verified_state())
    assert(bridge.current_state()["state"]==5)
    panel.present(bridge.current_state(),bridge.sites())
    assert(panel._actions["BOARD"].disabled)
    assert(not panel._actions["DESCEND"].disabled)
    assert(panel._actions["EXIT"].disabled==false)
    # Source identity is mandatory even if incoming source generation rises.
    var stale=BRIDGE.new()
    stale._active=true
    stale._pid=-1
    stale._accept("TSITE|1|88|0|123|17|36|99|0")
    stale._accept("TFRAME|1|88|0|3|0|123|17|35|0|5|8|99|0|0|0|0|0|1")
    assert(not stale.is_active())
    # A stale or duplicated tick must lock, never animate a fake shuttle.
    bridge._accept("TFRAME|1|77|0|3|5|123|17|36|0|6|9|99|0|0|0|0|0|1")
    assert(not bridge.is_active())
    print("S6-F5-F Godot shuttle packet/panel source admission PASS")
    quit(0)
