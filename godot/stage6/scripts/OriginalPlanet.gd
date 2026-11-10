extends Node3D
## Godot 4 visual adapter for SG-STAGE6-GODOT-01 original-world packets.
## Native Simulation/PersistentWorld owns ALL terrain, collisions-to-game,
## player/NPC arrival and resources. This adapter NEVER writes a game state.
const VISUAL_RADIUS: float = 1000.0
var loaded_generation: String = ""
var loaded_hash: String = ""

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
    load_verified_packet(args[1])

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
    if generation.is_empty() or world_hash.is_empty() or body.is_empty():
        push_error("Missing original world identity")
        return
    # This is a static copy. Live applications MUST discard tiles if the
    # native world generation/hash changes; a rendering client cannot decide
    # that a stale native observation is current.
    if not loaded_generation.is_empty() and (loaded_generation != generation or loaded_hash != world_hash):
        for existing in get_tree().get_nodes_in_group("original_terrain"):
            existing.queue_free()
    loaded_generation = generation
    loaded_hash = world_hash
    for tile in tiles:
        if typeof(tile) != TYPE_DICTIONARY:
            push_error("Invalid terrain tile entry")
            return
        _append_native_tile(tile)
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
