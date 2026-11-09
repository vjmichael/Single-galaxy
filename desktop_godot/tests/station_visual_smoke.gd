extends SceneTree
## PLAY-STATION-02: real Godot scene/parser/physics node smoke.
## Frames are SYNTHETIC, never interpreted as native SQLite proof.
## Native station->cockpit pipe and receipt custody are separately CTested.
func _initialize() -> void:
    call_deferred("_run")

func _fail(reason: String) -> void:
    push_error("PLAY-STATION-02 GODOT visual smoke FAIL: " + reason)
    quit(1)

func _is_active(bodies: Array, expected: bool) -> bool:
    for obj in bodies:
        if obj is CollisionObject3D:
            if (obj.collision_layer != 0) != expected:
                return false
            if (obj.collision_mask != 0) != expected:
                return false
    return true

func _run() -> void:
    var scene := load("res://world/main.tscn") as PackedScene
    if scene == null:
        _fail("cannot load main scene")
        return
    var stage := scene.instantiate()
    root.add_child(stage)
    await process_frame
    var station := stage.get_node_or_null("StationInterior_Prototype") as Node3D
    var cabin := stage.get_node_or_null("ShipInterior_Prototype") as Node3D
    var player := stage.get_node_or_null("LocalGrayboxPerson_NOT_NATIVE_PLAYER") as CharacterBody3D
    if station == null or cabin == null or player == null:
        _fail("missing station/cabin/player node")
        return
    var station_bodies := station.find_children("*", "StaticBody3D", true, false)
    var cabin_bodies := cabin.find_children("*", "StaticBody3D", true, false)
    if station_bodies.size() < 10 or cabin_bodies.size() < 5:
        _fail("station/cabin real collider inventory incomplete")
        return
    if station.get_node_or_null("Station_Hangar_Entry_At_Zero") == null:
        _fail("hangar entry hatch missing")
        return
    if not station.visible or cabin.visible:
        _fail("scene does not initially present station")
        return
    if not _is_active(station_bodies, true) or not _is_active(cabin_bodies, false):
        _fail("initial station collision custody invalid")
        return

    # Synthesize the *visual consumer* of an already-verified original PERSON.
    # This test does not write SQLite or grant ship authority to the UI.
    stage._on_native_station_person({"state":4,"station":1,"ship":0,
        "eligible_ship":2,"x_mm":1600,"z_mm":0})
    if not station.visible or cabin.visible or absf(player.global_position.x - 1.6) > 0.001:
        _fail("station PERSON was not projected to station geometry")
        return
    stage._on_native_station_person({"state":0,"station":1,"ship":2,
        "eligible_ship":2,"x_mm":0,"z_mm":0})
    if station.visible or not cabin.visible:
        _fail("BOARD PERSON did not switch visible location")
        return
    if not _is_active(station_bodies, false) or not _is_active(cabin_bodies, true):
        _fail("invisible station walls still block boarded player")
        return
    stage._on_native_player_pose({"x_mm":0,"y_mm":0,"z_mm":0,"seated":true})
    if not player.native_seated or not cabin.visible:
        _fail("ship POSE did not preserve cockpit authority projection")
        return
    stage._on_native_station_person({"state":4,"station":1,"ship":0,
        "eligible_ship":2,"x_mm":0,"z_mm":0})
    if not station.visible or cabin.visible or player.native_seated:
        _fail("disembark PERSON did not restore station and clear seat")
        return
    if not _is_active(station_bodies, true) or not _is_active(cabin_bodies, false):
        _fail("disembark collision layer restoration failed")
        return
    print("PLAY-STATION-02 GODOT visual smoke PASS: station/hangar, verified-frame projection, boarding/disembark, two-way collision visibility (synthetic frames; not native runtime)")
    quit(0)
