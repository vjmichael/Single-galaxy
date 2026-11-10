extends Node3D
## Godot 4 visual adapter for SG-STAGE6-GODOT-01 original-world packets.
## Native Simulation/PersistentWorld owns ALL terrain, collisions-to-game,
## player/NPC arrival and resources. This adapter NEVER writes a game state.
const VISUAL_RADIUS: float = 1000.0
const MAX_RESIDENT_TILES: int = 12
const REFRESH_INTERVAL: float = 0.5
# Native exporter publishes one atomic, <=4-tile packet per request.
# Cache/eviction is VISUAL ONLY; original world authority is never in Godot.
var _packet_path: String = ""
var _packet_digest: String = ""
var _refresh_elapsed: float = 0.0
var _resident_tiles: Dictionary = {}
var _resident_order: Array[String] = []
var _source_body: String = ""
var _source_owner: String = ""
var _source_ship: String = ""
var loaded_generation: String = ""
var loaded_hash: String = ""
# Client frame pacing only; does not infer physical R9700 performance or
# GPU frame time on machines where that hardware is not actually installed.
var _frame_ms: Array[float] = []
var _completed_measurement: bool = false

func _process(delta: float) -> void:
    if not _packet_path.is_empty() and delta > 0.0:
        _refresh_elapsed += delta
        if _refresh_elapsed >= REFRESH_INTERVAL:
            _refresh_elapsed = 0.0
            _reload_if_changed()
    if loaded_generation.is_empty() or _completed_measurement:
        return
    if delta <= 0.0:
        return
    _frame_ms.append(delta * 1000.0)
    if _frame_ms.size() < 300:
        return
    _frame_ms.sort()
    var n := _frame_ms.size()
    var data := {
        "schema": "SG-STAGE6-FRAME-01",
        "original_generation": loaded_generation,
        "original_hash": loaded_hash,
        "original_body_id": _source_body,
        "resident_tiles": _resident_tiles.size(),
        "video_adapter": RenderingServer.get_video_adapter_name(),
        "video_adapter_vendor": RenderingServer.get_video_adapter_vendor(),
        "rendering_driver": RenderingServer.get_current_rendering_driver_name(),
        "measured_frames": n,
        "p50_ms": _frame_ms[int(floor(float(n - 1) * 0.50))],
        "p95_ms": _frame_ms[int(floor(float(n - 1) * 0.95))],
        "p99_ms": _frame_ms[int(floor(float(n - 1) * 0.99))],
        "max_ms": _frame_ms[n - 1],
        "engine": Engine.get_version_info().get("string", "unknown"),
        "measurement": "Godot client frame deltas, not GPU execution time"
    }
    var result := FileAccess.open("user://stage6_frame_pacing.json", FileAccess.WRITE)
    if result != null:
        result.store_string(JSON.stringify(data, "  "))
        result.flush()
        print("Original Stage6 Godot frame pacing captured: user://stage6_frame_pacing.json")
    _completed_measurement = true


func _ready() -> void:
    var camera := Camera3D.new()
    camera.position = Vector3(0.0, 0.0, 2500.0)
    camera.current = true
    add_child(camera)
    camera.look_at(Vector3.ZERO)
    var sunlight := DirectionalLight3D.new()
    sunlight.rotation_degrees = Vector3(-35.0, -30.0, 0.0)
    add_child(sunlight)
    var args := OS.get_cmdline_user_args()
    if args.size() != 2 or args[0] != "--packet":
        push_warning("Specify -- --packet <original-world-stage6.json>; no invented planet displayed")
        return
    _packet_path = args[1]
    _reload_if_changed()

func _reload_if_changed() -> void:
    if not FileAccess.file_exists(_packet_path):
        return
    var digest := FileAccess.get_sha256(_packet_path)
    if digest.is_empty() or digest == _packet_digest:
        return
    # A packet can be refreshed by the original native exporter. Hash each
    # input file, rather than assuming mtime ticks are fine-grained enough.
    load_verified_packet(_packet_path)
    _packet_digest = digest

func _tile_key(tile: Dictionary) -> String:
    return str(int(tile.get("lat_band", -1))) + ":" + str(int(tile.get("lon_band", -1)))

func _valid_original_tile(tile: Dictionary) -> bool:
    for field in ["lat_band", "lon_band", "stride"]:
        var raw: Variant = tile.get(field, null)
        if typeof(raw) != TYPE_INT and typeof(raw) != TYPE_FLOAT:
            return false
        if not is_finite(float(raw)) or float(raw) != floor(float(raw)):
            return false
    var lat := int(tile["lat_band"])
    var lon := int(tile["lon_band"])
    var stride := int(tile["stride"])
    if lat < 0 or lat >= 5 or lon < 0 or lon >= 12 or (stride != 1 and stride != 2):
        return false
    var positions: Variant = tile.get("vertices", [])
    var indices: Variant = tile.get("triangles", [])
    if typeof(positions) != TYPE_ARRAY or typeof(indices) != TYPE_ARRAY:
        return false
    if positions.size() < 4 or positions.size() > 512 or indices.size() < 6 or indices.size() > 3072 or indices.size() % 3 != 0:
        return false
    for source in positions:
        if typeof(source) != TYPE_ARRAY or source.size() != 3:
            return false
        for component in source:
            if typeof(component) != TYPE_INT and typeof(component) != TYPE_FLOAT:
                return false
        if not Vector3(float(source[0]), float(source[1]), float(source[2])).is_finite():
            return false
    for raw_index in indices:
        if typeof(raw_index) != TYPE_INT and typeof(raw_index) != TYPE_FLOAT:
            return false
        if not is_finite(float(raw_index)) or float(raw_index) != floor(float(raw_index)):
            return false
        if int(raw_index) < 0 or int(raw_index) >= positions.size():
            return false
    return true

func load_verified_packet(packet_path: String) -> void:
    if not FileAccess.file_exists(packet_path):
        push_error("Missing original-world terrain packet")
        return
    var file := FileAccess.open(packet_path, FileAccess.READ)
    if file == null:
        push_error("Unable to open original-world terrain packet")
        return
    var decoded: Variant = JSON.parse_string(file.get_as_text())
    if typeof(decoded) != TYPE_DICTIONARY:
        push_error("Invalid original terrain packet")
        return
    var data: Dictionary = decoded
    if data.get("schema", "") != "SG-STAGE6-GODOT-01":
        push_error("Unknown native terrain packet schema")
        return
    var tiles: Variant = data.get("tiles", [])
    if typeof(tiles) != TYPE_ARRAY or tiles.is_empty() or tiles.size() > 4:
        push_error("Unbounded or empty native terrain tile batch")
        return
    var generation := str(data.get("generation", ""))
    var world_hash := str(data.get("world_hash", ""))
    var body := str(data.get("body_id", ""))
    var owner := str(data.get("owner", ""))
    var source_ship := str(data.get("source_ship", ""))
    if generation.is_empty() or world_hash.is_empty() or body.is_empty() or owner.is_empty() or source_ship.is_empty():
        push_error("Missing original native world/owner/craft identity")
        return
    # Validate the WHOLE batch before publishing any mesh or evicting old
    # geometry. A malformed or duplicate packet cannot partially render.
    var seen: Dictionary = {}
    for tile in tiles:
        if typeof(tile) != TYPE_DICTIONARY or not _valid_original_tile(tile):
            push_error("Invalid original terrain tile")
            return
        var key := _tile_key(tile)
        if seen.has(key):
            push_error("Duplicate original terrain tile or conflicting LOD")
            return
        seen[key] = true
    var changed := not loaded_generation.is_empty() and (
        loaded_generation != generation or loaded_hash != world_hash or
        _source_body != body or _source_owner != owner or _source_ship != source_ship)
    if changed:
        # Original generation/hash is the ONLY tile lifetime identity.
        # This is not native collision/landing authority.
        for old_tile in _resident_tiles.values():
            if is_instance_valid(old_tile):
                old_tile.queue_free()
        _resident_tiles.clear()
        _resident_order.clear()
        _frame_ms.clear()
        _completed_measurement = false
    loaded_generation = generation
    loaded_hash = world_hash
    _source_body = body
    _source_owner = owner
    _source_ship = source_ship
    for tile in tiles:
        _append_native_tile(tile)
    # Measure 300 stable client frames after the latest terrain-cache change,
    # instead of mixing page-in spikes with a different resident tile count.
    _frame_ms.clear()
    _completed_measurement = false
    # There is no player teleport here. A true descent receipt remains an
    # ORIGINAL native server authority, not a height-map or collision event.
    if data.get("landing_verified", false) == true:
        print("Original native shuttle descent receipt: ", data.get("original_descent_receipt", ""))
    else:
        print("Read-only original planet; no native player landing receipt")

func _append_native_tile(tile: Dictionary) -> void:
    var positions: Variant = tile.get("vertices", [])
    var indices: Variant = tile.get("triangles", [])
    if typeof(positions) != TYPE_ARRAY or typeof(indices) != TYPE_ARRAY:
        push_error("Invalid terrain vertex/index arrays")
        return
    if positions.size() < 4 or positions.size() > 512 or indices.size() < 6 or indices.size() > 3072 or indices.size() % 3 != 0:
        push_error("Invalid bounded native geometry")
        return
    var vertices := PackedVector3Array()
    for source in positions:
        if typeof(source) != TYPE_ARRAY or source.size() != 3:
            push_error("Invalid original vertex")
            return
        var v := Vector3(float(source[0]), float(source[1]), float(source[2])) * VISUAL_RADIUS
        if not v.is_finite():
            push_error("Nonfinite original terrain geometry")
            return
        vertices.append(v)
    var triangles := PackedInt32Array()
    var faces := PackedVector3Array()
    for index in indices:
        var n := int(index)
        if n < 0 or n >= vertices.size():
            push_error("Invalid original terrain index")
            return
        triangles.append(n)
        faces.append(vertices[n])
    var arrays := []
    arrays.resize(Mesh.ARRAY_MAX)
    arrays[Mesh.ARRAY_VERTEX] = vertices
    arrays[Mesh.ARRAY_INDEX] = triangles
    var mesh := ArrayMesh.new()
    mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
    var material := StandardMaterial3D.new()
    material.albedo_color = Color(0.38, 0.49, 0.46)
    material.cull_mode = BaseMaterial3D.CULL_DISABLED
    mesh.surface_set_material(0, material)
    var surface := MeshInstance3D.new()
    surface.mesh = mesh
    surface.add_to_group("original_terrain")
    add_child(surface)
    # Trimesh collision is drawn from identical authenticated triangles.
    # It is a visual-client StaticBody only, not a player landing grant.
    var collider := StaticBody3D.new()
    var shape := ConcavePolygonShape3D.new()
    shape.set_faces(faces)
    var collision := CollisionShape3D.new()
    collision.shape = shape
    collider.add_child(collision)
    surface.add_child(collider)
    # Keep one visual LOD for each original band. Incoming native batches
    # replace the same band and evict the oldest after twelve resident tiles.
    var key := _tile_key(tile)
    if _resident_tiles.has(key):
        var replaced: Node = _resident_tiles[key]
        replaced.queue_free()
        _resident_order.erase(key)
    _resident_tiles[key] = surface
    _resident_order.append(key)
    while _resident_order.size() > MAX_RESIDENT_TILES:
        var victim: String = _resident_order.pop_front()
        var node: Node = _resident_tiles[victim]
        node.queue_free()
        _resident_tiles.erase(victim)
