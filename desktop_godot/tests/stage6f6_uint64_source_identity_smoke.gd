extends SceneTree
## S6-F6: source precision regression. Static Godot packets only, NOT a
## true native running save or shipped Windows acceptance.
const SOURCE := preload("res://world/native_source_id.gd")
const RECOVERY := preload("res://world/native_planet_recovery_probe.gd")
const TRANSFER := preload("res://world/native_planet_transfer_session.gd")
const MAX_BODY := "18446744073709551615"
const ABOVE_SIGNED := "9223372036854775808"

func _initialize() -> void:
    call_deferred("_run")

func _run() -> void:
    assert(SOURCE.valid(MAX_BODY))
    assert(SOURCE.valid(ABOVE_SIGNED))
    assert(not SOURCE.valid("18446744073709551616"))
    assert(not SOURCE.valid("-1"))
    assert(not SOURCE.valid("+1"))
    assert(not SOURCE.valid("00002"))
    assert(not SOURCE.valid("0"))
    assert(SOURCE.valid("0",true))
    assert(SOURCE.same(MAX_BODY,MAX_BODY))
    assert(not SOURCE.same(MAX_BODY,ABOVE_SIGNED))
    var frame := "RFRAME|1|4|7|42|%s|17|36|818|2|110|30" % MAX_BODY
    var source: Dictionary=RECOVERY.parse_frame(frame)
    assert(str(source.get("body",""))==MAX_BODY)
    assert(int(source.get("role",-1))==4)
    assert(RECOVERY.parse_frame(
        "RFRAME|1|4|7|42|18446744073709551616|17|36|818|2|110|30").is_empty())
    var transfer=TRANSFER.new()
    transfer._active=true # synthetic packet ingress only, no native writer
    transfer._pid=-1
    transfer._accept("TSITE|1|77|0|%s|17|36|99|0" % MAX_BODY)
    transfer._accept("TFRAME|1|77|0|3|0|%s|17|36|0|5|8|99|0|0|0|0|0|1" % MAX_BODY)
    assert(transfer.has_verified_state())
    assert(str(transfer.current_state().get("body",""))==MAX_BODY)
    assert(str(transfer.sites()[0]["body"])==MAX_BODY)
    var forged=TRANSFER.new()
    forged._active=true
    forged._pid=-1
    forged._accept("TSITE|1|88|0|18446744073709551616|17|36|99|0")
    assert(not forged.is_active())
    print("S6-F6 lossless original uint64 body identity in recovery/shuttle PASS")
    quit(0)
