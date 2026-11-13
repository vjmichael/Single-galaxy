extends SceneTree
## Hosted installed gate: mesh comes from real source-owned C++ 3x3 atlas,
## not synthetic Godot terrain. Physical GPU approval remains separate.
const TERRAIN := preload("res://world/native_planet_terrain.gd")

func _run() -> void:
    var path: String=OS.get_environment("SG_STAGE6G_PACKET")
    if path.is_empty() or not path.is_absolute_path() or not FileAccess.file_exists(path):
        push_error("S6-G requires actual native original-world packet")
        quit(2)
        return
    var input := FileAccess.open(path,FileAccess.READ)
    if input==null or input.get_length()>8192:
        push_error("Invalid bounded original packet")
        quit(2)
        return
    var value: Variant=JSON.parse_string(input.get_as_text())
    input.close()
    if typeof(value)!=TYPE_DICTIONARY or value.get("schema","")!="SG-S6G-ORIGINAL-TERRAIN-01":
        push_error("Wrong native packet identity")
        quit(2)
        return
    var native: Dictionary=value
    var atlas: String=str(native.get("atlas_path",""))
    if not atlas.is_absolute_path() or not atlas.ends_with(".stage6.walkpatch.json") or \
       not FileAccess.file_exists(atlas) or \
       FileAccess.get_sha256(atlas)!=str(native.get("atlas_sha256","")):
        push_error("Native source atlas missing or SHA-256 mismatch")
        quit(2)
        return
    var pose: Dictionary=native.get("pose",{})
    if pose.is_empty() or int(pose.get("session",0))<=0 or \
       str(pose.get("body","0"))=="0" or int(pose.get("gen",0))<=0:
        push_error("Missing original writer PFRAME")
        quit(2)
        return
    var terrain := TERRAIN.new()
    root.add_child(terrain)
    await process_frame
    terrain.set_source(atlas)
    terrain.accept_pose(pose)
    var mesh: MeshInstance3D=terrain.get_node("OriginalNativeTerrainTileWindow_NO_COLLISION")
    var accepted: bool= terrain.tile_count()==9 and mesh.visible and mesh.mesh!=null and \
        mesh.mesh.get_surface_count()==1 and \
        mesh.mesh.surface_get_array_len(0)==9*289
    var stale: Dictionary=pose.duplicate(true)
    stale["session"]=int(pose["session"])+1
    terrain.accept_pose(stale)
    var denied: bool= not mesh.visible and terrain.tile_count()==0
    terrain.clear()
    terrain.queue_free()
    if not accepted or not denied:
        push_error("Original 3x3 installed terrain or session-denial failed")
        quit(1)
        return
    print("S6-G installed original C++ 3x3 terrain atlas, native PFRAME and stale-session denial PASS")
    quit(0)

func _initialize() -> void:
    call_deferred("_run")
