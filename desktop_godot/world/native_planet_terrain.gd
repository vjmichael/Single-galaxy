extends Node3D
## S6-F5-C: actual installed-app near-field terrain mesh from the *original*
## signed-mm source-triangle sampler in Stage6WalkingPatch.hpp.
## Pure rendering. NEVER submits collisions, jumps, heights, or player authority.
const GRID_WIDTH := 17
const MAX_VERTICES := 289
const MAX_TRIANGLE_INDICES := 1536
const MAX_CENTER_DISTANCE_MM := 32000
var _source_path := ""
var _digest := ""
var _last_check_msec := 0
var _pose: Dictionary = {}
var _mesh_instance: MeshInstance3D

func _ready() -> void:
    _mesh_instance = MeshInstance3D.new()
    _mesh_instance.name = "VerifiedOriginalTerrain_NO_GAME_COLLISION"
    _mesh_instance.visible = false
    _mesh_instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
    var material := StandardMaterial3D.new()
    material.albedo_color = Color(0.38, 0.42, 0.35)
    material.roughness = 1.0
    material.cull_mode = BaseMaterial3D.CULL_DISABLED
    _mesh_instance.material_override = material
    add_child(_mesh_instance)

func set_source(path: String) -> void:
    clear()
    if path.is_absolute_path() and path.ends_with(".stage6.walkpatch.json"):
        _source_path=path

func accept_pose(pose: Dictionary) -> void:
    # Source identity is already admitted by native_planet_surface_session.
    # This viewer only uses it to reject stale and unrelated packet geometry.
    if _pose.get("body",0)!=pose.get("body",0) or \
       _pose.get("ship",0)!=pose.get("ship",0) or \
       _pose.get("site",0)!=pose.get("site",0):
        _digest=""
        if _mesh_instance!=null:
            _mesh_instance.visible=false
    _pose=pose.duplicate(true)
    _load_if_changed()
    _check_range()

func clear() -> void:
    _pose.clear()
    _source_path=""
    _digest=""
    if _mesh_instance!=null:
        _mesh_instance.visible=false
        _mesh_instance.mesh=null

func _process(_delta: float) -> void:
    if _source_path.is_empty() or _pose.is_empty():
        return
    if Time.get_ticks_msec()-_last_check_msec<350:
        return
    _last_check_msec=Time.get_ticks_msec()
    _load_if_changed()
    _check_range()

func _integer_string(value: Variant) -> bool:
    return typeof(value)==TYPE_STRING and not str(value).is_empty() and \
        str(value).length()<=20 and str(value).is_valid_int()

func _load_if_changed() -> void:
    if _source_path.is_empty() or not FileAccess.file_exists(_source_path):
        if _mesh_instance!=null:
            _mesh_instance.visible=false
        return
    var digest := FileAccess.get_sha256(_source_path)
    if digest.is_empty() or digest==_digest:
        return
    var file := FileAccess.open(_source_path,FileAccess.READ)
    if file==null or file.get_length()>100000:
        if _mesh_instance!=null:
            _mesh_instance.visible=false
        return
    var parsed: Variant=JSON.parse_string(file.get_as_text())
    if typeof(parsed)!=TYPE_DICTIONARY:
        if _mesh_instance!=null:
            _mesh_instance.visible=false
        return
    var data: Dictionary=parsed
    var required := ["body","owner","ship","surface_location",
        "native_generation","native_tick","native_move_receipt",
        "center_x_mm","center_y_mm","center_z_mm"]
    for key in required:
        if not _integer_string(data.get(key,null)):
            _mesh_instance.visible=false
            return
    if data.get("schema","")!="SG-STAGE6-WALKPATCH-01" or \
       int(data.get("width",0))!=GRID_WIDTH or \
       int(data.get("stride_mm",0))!=4000 or \
       int(data["owner"])!=7 or \
       int(data["body"])!=int(_pose.get("body",-1)) or \
       int(data["ship"])!=int(_pose.get("ship",-1)) or \
       int(data["surface_location"])!=int(_pose.get("site",-1)) or \
       int(data["native_generation"])>int(_pose.get("gen",-1)) or \
       int(data["native_tick"])>int(_pose.get("tick",-1)) or \
       int(data["native_move_receipt"])>int(_pose.get("receipt",-1)):
        _mesh_instance.visible=false
        return
    var positions: Variant=data.get("vertices",null)
    var triangles: Variant=data.get("triangles",null)
    if typeof(positions)!=TYPE_ARRAY or typeof(triangles)!=TYPE_ARRAY or \
       positions.size()!=MAX_VERTICES or \
       triangles.size()!=MAX_TRIANGLE_INDICES:
        _mesh_instance.visible=false
        return
    var vertex_data := PackedVector3Array()
    for v in positions:
        if typeof(v)!=TYPE_ARRAY or v.size()!=3:
            _mesh_instance.visible=false
            return
        for n in v:
            if (typeof(n)!=TYPE_INT and typeof(n)!=TYPE_FLOAT) or \
               not is_finite(float(n)):
                _mesh_instance.visible=false
                return
        var point := Vector3(float(v[0]),float(v[1]),float(v[2]))
        if absf(point.x)>32.01 or absf(point.z)>32.01 or \
           absf(point.y)>1000.0:
            _mesh_instance.visible=false
            return
        vertex_data.append(point)
    var index_data := PackedInt32Array()
    for raw in triangles:
        if (typeof(raw)!=TYPE_INT and typeof(raw)!=TYPE_FLOAT) or \
           not is_finite(float(raw)) or float(raw)!=floor(float(raw)) or \
           int(raw)<0 or int(raw)>=MAX_VERTICES:
            _mesh_instance.visible=false
            return
        index_data.append(int(raw))
    var source_arrays := []
    source_arrays.resize(Mesh.ARRAY_MAX)
    source_arrays[Mesh.ARRAY_VERTEX]=vertex_data
    source_arrays[Mesh.ARRAY_INDEX]=index_data
    var mesh := ArrayMesh.new()
    mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES,source_arrays)
    _mesh_instance.mesh=mesh
    _mesh_instance.position=Vector3(
        float(int(data["center_x_mm"]))/1000.0,
        float(int(data["center_y_mm"]))/1000.0,
        float(int(data["center_z_mm"]))/1000.0)
    _mesh_instance.set_meta("source_center_x_mm",int(data["center_x_mm"]))
    _mesh_instance.set_meta("source_center_z_mm",int(data["center_z_mm"]))
    _mesh_instance.set_meta("source_generation",int(data["native_generation"]))
    _digest=digest
    _check_range()

func _check_range() -> void:
    if _mesh_instance==null or _mesh_instance.mesh==null or _pose.is_empty():
        return
    var cx := int(_mesh_instance.get_meta("source_center_x_mm",0))
    var cz := int(_mesh_instance.get_meta("source_center_z_mm",0))
    _mesh_instance.visible=abs(int(_pose.get("x_mm",0))-cx)<=MAX_CENTER_DISTANCE_MM and \
        abs(int(_pose.get("z_mm",0))-cz)<=MAX_CENTER_DISTANCE_MM
