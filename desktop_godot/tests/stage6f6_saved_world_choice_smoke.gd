extends SceneTree
## S6-F6: standalone UI path preference check. The file here is an isolated
## test-only hint, never original SQLite, a human grant or movement receipt.
const CHOICE := preload("res://world/native_saved_world_choice.gd")
func _initialize() -> void:
    call_deferred("_run")

func _run() -> void:
    var base: String=OS.get_user_data_dir()
    var hint: String=base.path_join("s6f6-synthetic-selected-original.json")
    var world: String=base.path_join("s6f6-synthetic-original.sqlite")
    assert(CHOICE.safe_path(world))
    assert(not CHOICE.safe_path("relative.sqlite"))
    assert(not CHOICE.safe_path(base.path_join("not-sqlite.txt")))
    assert(not CHOICE.safe_path(world+"\n"))
    assert(CHOICE.remember(world,hint))
    var saved: Dictionary=CHOICE.read_hint(hint)
    assert(saved.get("status","")=="ok")
    assert(str(saved.get("save",""))==world)
    assert(CHOICE.forget(hint))
    assert(CHOICE.read_hint(hint).get("status","")=="none")
    # A corrupted UI hint must not silently grant genesis or another world.
    var f := FileAccess.open(hint,FileAccess.WRITE)
    assert(f!=null)
    f.store_string("{\"schema\":\"BAD\",\"save\":\"/tmp/other.sqlite\"}")
    f.close()
    assert(CHOICE.read_hint(hint).get("status","")=="invalid")
    assert(CHOICE.forget(hint))
    print("S6-F6 Godot original-world selected-path preference safety PASS")
    quit(0)
