extends SceneTree
## S6-F6: source packet only; NOT real SQLite, host supervision or human
## installed-game acceptance. Native subprocess evidence has a separate test.
const PROBE := preload("res://world/native_planet_recovery_probe.gd")
func _initialize() -> void:
    call_deferred("_verify")

func _verify() -> void:
    # role, original person state, ship, body, 36x72 cell, actual site,
    # source shuttle phase and original SQLite tick/generation
    var station=PROBE.parse_frame("RFRAME|1|1|4|0|0|0|0|0|0|101|22")
    assert(int(station.get("role",0))==1)
    assert(int(station.get("state",-1))==4)
    var ship=PROBE.parse_frame("RFRAME|1|2|0|42|0|0|0|0|0|105|25")
    assert(int(ship.get("role",0))==2)
    var transit=PROBE.parse_frame("RFRAME|1|3|6|42|991|17|36|818|1|106|26")
    assert(int(transit.get("role",0))==3)
    assert(int(transit.get("state",0))==6)
    var ashore=PROBE.parse_frame("RFRAME|1|4|7|42|991|17|36|818|2|110|30")
    assert(int(ashore.get("role",0))==4)
    var denied=[
        "RFRAME|1|4|7|42|991|17|36|818|1|110|30", # no surface in transit
        "RFRAME|1|4|5|42|991|17|36|818|2|110|30", # shuttle not surface
        "RFRAME|1|3|6|42|991|36|36|818|1|110|30", # invalid latitude
        "RFRAME|1|3|6|42|991|17|72|818|1|110|30", # invalid longitude
        "RFRAME|1|3|6|42|991|17|36|0|1|110|30", # no real site
        "RFRAME|1|4|7|42|991|17|36|818|2|110|0", # no SQLite generation
        "RFRAME|1|4|7|42|991|17|36|818|2|110|-1", # invalid negative
        "RFRAME|1|4|7|42|991|17|36|818|2|110|30|99", # extra token
        "RFRAME|1|2|0|42|991|17|36|818|2|110|30", # ship with planet
        "RFRAME|1|3|7|42|991|17|36|818|2|110|30", # surface routed shuttle
        "RFRAME|1|3|6|42|991|17|36|818|3|110|30|", # changed packet
    ]
    for p in denied:
        assert(PROBE.parse_frame(p).is_empty(),p)
    print("S6-F6 Godot native recovery role packet rejection PASS")
    quit(0)
