extends Node3D
## S6-F5-E: bounded original-world tiles, as one NON-COLLIDING render mesh.
## A new original host session must match before even a cached file is shown.
## This view neither invents terrain nor feeds a Godot physics ground contact.
const SOURCE_ID := preload("res://world/native_source_id.gd")
const GRID_WIDTH := 17
const VERTICES_PER_TILE := 289
const INDICES_PER_TILE := 1536
const MAX_TILES := 9
const MAX_PACKET_BYTES := 1000000
const MAX_CENTER_DISTANCE_MM := 112000
var _source_path := ""
var _digest := ""
var _last_check_msec := 0
var _pose: Dictionary = {}
var _visual_origin_x_mm := 0
var _visual_origin_z_mm := 0
var _mesh_instance: MeshInstance3D

func _ready() -> void:
    _mesh_instance=MeshInstance3D.new()
    _mesh_instance.name="OriginalNativeTerrainTileWindow_NO_COLLISION"
    _mesh_instance.visible=false
    _mesh_instance.cast_shadow=GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
    var material := StandardMaterial3D.new()
    material.albedo_color=Color(0.38,0.42,0.35)
    material.roughness=1.0
    material.cull_mode=BaseMaterial3D.CULL_DISABLED
    _mesh_instance.material_override=material
    add_child(_mesh_instance)

func tile_count() -> int:
    return int(_mesh_instance.get_meta("source_tile_count",0)) if _mesh_instance!=null else 0

func set_visual_origin(x_mm: int,z_mm: int) -> void:
    _visual_origin_x_mm=x_mm
    _visual_origin_z_mm=z_mm
    _rebase()

func set_source(path: String) -> void:
    clear()
    if path.is_absolute_path() and path.ends_with(".stage6.walkpatch.json"):
        _source_path=path

func accept_pose(pose: Dictionary) -> void:
    # native_planet_surface_session has already verified the original PFRAME.
    if _pose.get("session",0)!=pose.get("session",0) or \
       _pose.get("body",0)!=pose.get("body",0) or \
       _pose.get("ship",0)!=pose.get("ship",0) or \
       _pose.get("site",0)!=pose.get("site",0):
        _digest=""
        _hide()
    _pose=pose.duplicate(true)
    _load_if_changed()
    _rebase()

func clear() -> void:
    _pose.clear()
    _source_path=""
    _digest=""
    _hide()

func _hide() -> void:
    if _mesh_instance!=null:
        _mesh_instance.visible=false
        _mesh_instance.mesh=null
        _mesh_instance.set_meta("source_tile_count",0)

func _process(_delta: float) -> void:
    if _source_path.is_empty() or _pose.is_empty():
        return
    if Time.get_ticks_msec()-_last_check_msec<350:
        return
    _last_check_msec=Time.get_ticks_msec()
    _load_if_changed()
    _rebase()

func _int_string(v: Variant) -> bool:
    return typeof(v)==TYPE_STRING and str(v).length()>0 and \
        str(v).length()<=20 and str(v).is_valid_int() and \
        not str(v).begins_with("+")

func _load_if_changed() -> void:
    if _source_path.is_empty() or not FileAccess.file_exists(_source_path):
        _hide()
        _digest=""
        return
    var digest := FileAccess.get_sha256(_source_path)
    if digest.is_empty() or digest==_digest:
        return
    var file := FileAccess.open(_source_path,FileAccess.READ)
    if file==null or file.get_length()>MAX_PACKET_BYTES:
        _hide()
        return
    var parsed: Variant=JSON.parse_string(file.get_as_text())
    if typeof(parsed)!=TYPE_DICTIONARY:
        _hide()
        return
    var envelope: Dictionary=parsed
    if envelope.get("schema","")!="SG-STAGE6-WALKTILES-01" or \
       not _int_string(envelope.get("session",null)) or \
       int(envelope["session"])!=int(_pose.get("session",0)):
        _hide()
        return
    var tiles: Variant=envelope.get("tiles",null)
    if typeof(tiles)!=TYPE_ARRAY or tiles.size()<1 or tiles.size()>MAX_TILES:
        _hide()
        return
    var vertex_data := PackedVector3Array()
    var index_data := PackedInt32Array()
    var anchor_x: int=0
    var anchor_z: int=0
    var centers: Dictionary={}
    for tile_raw in tiles:
        if typeof(tile_raw)!=TYPE_DICTIONARY:
            _hide()
            return
        var tile: Dictionary=tile_raw
        var required := ["body","owner","ship","surface_location",
            "native_generation","native_tick","native_move_receipt",
            "center_x_mm","center_y_mm","center_z_mm"]
        for key in required:
            if key=="body":
                if not SOURCE_ID.valid(tile.get(key,null)):
                    _hide()
                    return
                continue
            if not _int_string(tile.get(key,null)):
                _hide()
                return
        if tile.get("schema","")!="SG-STAGE6-WALKPATCH-01" or \
           int(tile.get("width",0))!=GRID_WIDTH or \
           int(tile.get("stride_mm",0))!=4000 or \
           int(tile["owner"])!=7 or \
           not SOURCE_ID.same(tile["body"],_pose.get("body","0")) or \
           int(tile["ship"])!=int(_pose.get("ship",-1)) or \
           int(tile["surface_location"])!=int(_pose.get("site",-1)) or \
           int(tile["native_generation"])>int(_pose.get("gen",-1)) or \
           int(tile["native_tick"])>int(_pose.get("tick",-1)) or \
           int(tile["native_move_receipt"])>int(_pose.get("receipt",-1)):
            _hide()
            return
        var cx: int=int(tile["center_x_mm"])
        var cy: int=int(tile["center_y_mm"])
        var cz: int=int(tile["center_z_mm"])
        # Guard malicious sidecar integers before subtraction/abs. Original
        # verified PFRAME X/Z are each bounded to +/-1e12 mm.
        if cx < -1000000000000 or cx > 1000000000000 or \
           cz < -1000000000000 or cz > 1000000000000 or \
           cy < -1000000000 or cy > 1000000000:
            _hide()
            return
        if abs(cx-int(_pose.get("x_mm",0)))>MAX_CENTER_DISTANCE_MM or \
           abs(cz-int(_pose.get("z_mm",0)))>MAX_CENTER_DISTANCE_MM:
            _hide()
            return
        var identity: String=str(cx)+":"+str(cz)
        if centers.has(identity):
            _hide()
            return
        centers[identity]=true
        if vertex_data.is_empty():
            anchor_x=cx
            anchor_z=cz
        var points: Variant=tile.get("vertices",null)
        var triangles: Variant=tile.get("triangles",null)
        if typeof(points)!=TYPE_ARRAY or typeof(triangles)!=TYPE_ARRAY or \
           points.size()!=VERTICES_PER_TILE or triangles.size()!=INDICES_PER_TILE:
            _hide()
            return
        var base: int=vertex_data.size()
        for i in range(VERTICES_PER_TILE):
            var v: Variant=points[i]
            if typeof(v)!=TYPE_ARRAY or v.size()!=3:
                _hide()
                return
            for n in v:
                if (typeof(n)!=TYPE_INT and typeof(n)!=TYPE_FLOAT) or \
                   not is_finite(float(n)):
                    _hide()
                    return
            var local := Vector3(float(v[0]),float(v[1]),float(v[2]))
            var row: int=i / GRID_WIDTH
            var col: int=i % GRID_WIDTH
            if absf(local.x-float(col-8)*4.0)>0.001 or \
               absf(local.z-float(row-8)*4.0)>0.001 or absf(local.y)>1000.0:
                _hide()
                return
            vertex_data.append(Vector3(
                float(cx-anchor_x)/1000.0+local.x,
                float(cy)/1000.0+local.y,
                float(cz-anchor_z)/1000.0+local.z))
        for raw in triangles:
            if typeof(raw)!=TYPE_INT or int(raw)<0 or int(raw)>=VERTICES_PER_TILE:
                _hide()
                return
            index_data.append(base+int(raw))
    var normals := PackedVector3Array()
    normals.resize(vertex_data.size())
    for start in range(0,index_data.size(),3):
        var a: int=index_data[start]
        var b: int=index_data[start+1]
        var c: int=index_data[start+2]
        var normal := (vertex_data[b]-vertex_data[a]).cross(
            vertex_data[c]-vertex_data[a]).normalized()
        normals[a]=normals[a]+normal
        normals[b]=normals[b]+normal
        normals[c]=normals[c]+normal
    for i in range(normals.size()):
        normals[i]=normals[i].normalized()
    var arrays := []
    arrays.resize(Mesh.ARRAY_MAX)
    arrays[Mesh.ARRAY_VERTEX]=vertex_data
    arrays[Mesh.ARRAY_NORMAL]=normals
    arrays[Mesh.ARRAY_INDEX]=index_data
    var mesh := ArrayMesh.new()
    mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES,arrays)
    _mesh_instance.mesh=mesh
    _mesh_instance.set_meta("source_center_x_mm",anchor_x)
    _mesh_instance.set_meta("source_center_z_mm",anchor_z)
    _mesh_instance.set_meta("source_tile_count",tiles.size())
    _digest=digest
    _rebase()

func _rebase() -> void:
    if _mesh_instance==null or _mesh_instance.mesh==null or _pose.is_empty():
        return
    var cx: int=int(_mesh_instance.get_meta("source_center_x_mm",0))
    var cz: int=int(_mesh_instance.get_meta("source_center_z_mm",0))
    _mesh_instance.position=Vector3(
        float(cx-_visual_origin_x_mm)/1000.0,0.0,
        float(cz-_visual_origin_z_mm)/1000.0)
    _mesh_instance.visible=abs(cx-int(_pose.get("x_mm",0)))<=MAX_CENTER_DISTANCE_MM and \
        abs(cz-int(_pose.get("z_mm",0)))<=MAX_CENTER_DISTANCE_MM
