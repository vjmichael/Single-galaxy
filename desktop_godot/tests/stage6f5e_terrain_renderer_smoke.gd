extends SceneTree
## S6-F5-E HEADLESS SOURCE-VIEW FIXTURE. Synthetic tile data checks the
## installed Godot decoder ONLY; this is not proof of genuine C++ authority,
## a physical player landing, Windows packaging or sustained GPU play.
const TERRAIN := preload("res://world/native_planet_terrain.gd")

func _tile(cx: int,cz: int) -> Dictionary:
    var vertices: Array=[]
    var triangles: Array=[]
    for row in range(17):
        for col in range(17):
            vertices.append([float(col-8)*4.0,0.0,float(row-8)*4.0])
    for row in range(16):
        for col in range(16):
            var a: int=row*17+col
            var b: int=(row+1)*17+col
            triangles.append_array([a,b,a+1,b,b+1,a+1])
    return {"schema":"SG-STAGE6-WALKPATCH-01",
        "body":"10","owner":"7","ship":"20","surface_location":"30",
        "native_generation":"11","native_tick":"22","native_move_receipt":"0",
        "center_x_mm":str(cx),"center_y_mm":"1000","center_z_mm":str(cz),
        "stride_mm":4000,"width":17,
        "vertices":vertices,"triangles":triangles}

func _write(path: String, data: Dictionary) -> bool:
    var file := FileAccess.open(path,FileAccess.WRITE)
    if file==null:
        return false
    file.store_string(JSON.stringify(data))
    file.flush()
    file.close()
    return true

func _run() -> void:
    var viewer := TERRAIN.new()
    root.add_child(viewer)
    await process_frame
    var path: String=OS.get_user_data_dir().path_join(
        "stage6f5e-renderer-test.stage6.walkpatch.json")
    var tiles: Array=[]
    for row in range(-1,2):
        for col in range(-1,2):
            tiles.append(_tile(col*64000,row*64000))
    var atlas := {"schema":"SG-STAGE6-WALKTILES-01",
        "session":"55","tiles":tiles}
    var pose := {"session":55,"body":"10","ship":20,"site":30,
        "x_mm":0,"z_mm":0,"gen":11,"tick":22,"receipt":0}
    if not _write(path,atlas):
        push_error("Could not stage test-only bounded atlas")
        quit(1)
        return
    viewer.set_source(path)
    viewer.accept_pose(pose)
    var mesh: MeshInstance3D=viewer.get_node(
        "OriginalNativeTerrainTileWindow_NO_COLLISION")
    var admitted := viewer.tile_count()==9 and mesh.visible and \
        mesh.mesh!=null and mesh.mesh.surface_get_array_len(0)==9*289 and \
        viewer.get_child_count()==1
    viewer.set_visual_origin(64000,0)
    admitted=admitted and absf(mesh.position.x+128.0)<0.001
    var wrong_epoch: Dictionary=pose.duplicate(true)
    wrong_epoch["session"]=56
    viewer.accept_pose(wrong_epoch)
    var rejects_epoch := not mesh.visible and viewer.tile_count()==0
    # Stale or mixed-original packets must never generate a guessed floor.
    atlas["session"]="56"
    tiles[4]["body"]="999"
    var staged := _write(path,atlas)
    viewer.accept_pose(wrong_epoch)
    var rejects_body := not mesh.visible and viewer.tile_count()==0
    viewer.clear()
    DirAccess.remove_absolute(path)
    viewer.queue_free()
    if not (admitted and rejects_epoch and rejects_body and staged):
        push_error("Stage6F5E native tile viewer source-contract failure")
        quit(1)
        return
    print("S6-F5-E Godot-only terrain session/identity/extent acceptance PASS")
    quit(0)

func _initialize() -> void:
    call_deferred("_run")
