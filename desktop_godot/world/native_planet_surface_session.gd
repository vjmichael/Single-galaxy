extends RefCounted
## S6-F5-C: supervised original-world planet mode of the installed C++ host.
## The original player, source body, altitude, receipt, and SQLite generation
## are supplied ONLY by the verified C++ PFRAME. No Godot collision or local
## CharacterBody movement is ever submitted as authoritative.
const SGDX_LAUNCH := preload("res://world/sgdx_process_launch.gd")
signal pose_received(pose: Dictionary)
signal status_changed(message: String)
signal connection_changed(connected: bool)
const MAX_LINE := 512
var _active := false
var _pid := -1
var _last_shutdown_pid := -1
var _stdout: FileAccess
var _stderr: FileAccess
var _buffer := PackedByteArray()
var _session := 0
var _request := 1
var _pending := 0
var _last_send_ms := -100000
var _tick := -1
var _generation := -1
var _identity := ""
var _last_pose: Dictionary = {}

func is_active() -> bool:
    return _active

func has_verified_pose() -> bool:
    return _active and _session > 0 and not _identity.is_empty()

func process_id() -> int:
    if _active:
        return _pid
    if _last_shutdown_pid > 0 and OS.is_process_running(_last_shutdown_pid):
        return _last_shutdown_pid
    _last_shutdown_pid = -1
    return -1

func start(save: String) -> void:
    if _active or process_id() > 0:
        status_changed.emit("Planetary writer already active or not yet released")
        return
    if not save.is_absolute_path() or not FileAccess.file_exists(save):
        status_changed.emit("Planetary mode requires the existing original SQLite save")
        return
    var host := SGDX_LAUNCH.native_host_path("galaxy_desktop_player_host.exe" if OS.get_name()=="Windows" else "galaxy_desktop_player_host")
    var spec := SGDX_LAUNCH.supervised_command(save, "godot_planet", host, PackedStringArray([save, "--planet"]))
    if spec.is_empty():
        status_changed.emit("Verified native planetary host or SGDX supervisor unavailable")
        return
    var result := OS.execute_with_pipe(spec["path"], spec["args"], false)
    if not result.has("stdio") or not result.has("pid"):
        status_changed.emit("Planetary host failed to start")
        return
    _pid = result["pid"]
    _stdout = result["stdio"]
    _stderr = result.get("stderr")
    _active = true
    _session = 0
    _request = 1
    _pending = 0
    _tick = -1
    _generation = -1
    _identity = ""
    _last_pose.clear()
    _buffer.clear()
    connection_changed.emit(true)
    status_changed.emit("Verifying original planetary custody; no local movement")

func stop(reason: String="Planetary client closed", graceful: bool=false) -> void:
    if _pid > 0:
        _last_shutdown_pid = _pid
    if _active and _pid > 0 and OS.is_process_running(_pid):
        if graceful and _stdout != null:
            _stdout.store_line("QUIT")
            _stdout.flush()
        else:
            OS.kill(_pid) # stops the supervised custody tree
    _active = false
    _pid = -1
    _stdout = null
    _stderr = null
    _session = 0
    _pending = 0
    _identity = ""
    _last_pose.clear()
    _buffer.clear()
    connection_changed.emit(false)
    status_changed.emit(reason)

func poll() -> void:
    if not _active:
        return
    for n in range(16):
        var bytes := _stdout.get_buffer(4096)
        if bytes.is_empty():
            break
        for b in bytes:
            if b == 10:
                var line := _buffer.get_string_from_ascii()
                _buffer.clear()
                _accept(line)
            elif b == 13:
                continue
            elif b < 32 or b > 126 or _buffer.size() >= MAX_LINE:
                stop("Original planet protocol invalid; original save retained")
                return
            else:
                _buffer.append(b)
        if not _active:
            return
    if _pending != 0 and Time.get_ticks_msec() - _last_send_ms > 3000:
        stop("Planet movement ACK timed out; reconnect to recover original pose")
        return
    if not OS.is_process_running(_pid):
        stop("Planet host exited; no Godot-local fall back to movement")

func _integer(s: String) -> bool:
    return not s.is_empty() and s.length() <= 20 and s.is_valid_int() and not s.begins_with("+")

func _accept(line: String) -> void:
    var p := line.split("|", false)
    if p.size() == 4 and p[0]=="DENY" and p[1]=="1":
        if not _integer(p[2]):
            stop("Malformed planetary denial")
            return
        var id := int(p[2])
        if id != 0 and id == _pending:
            _pending = 0
        status_changed.emit("Original C++ refused planetary step #%d: %s" % [id, p[3]])
        return
    if p.size() != 18 or p[0]!="PFRAME" or p[1]!="1":
        stop("Unexpected original planetary host frame")
        return
    for i in range(2,18):
        if not _integer(p[i]):
            stop("Non-numeric original planetary pose")
            return
    var sess := int(p[2])
    var req := int(p[3])
    var ship := int(p[4])
    var body := int(p[5])
    var lat := int(p[6])
    var lon := int(p[7])
    var x := int(p[8])
    var y := int(p[9])
    var z := int(p[10])
    var lat_pdeg := int(p[11])
    var lon_pdeg := int(p[12])
    var move_tick := int(p[13])
    var receipt := int(p[14])
    var tick := int(p[15])
    var gen := int(p[16])
    var site := int(p[17])
    if sess<=0 or ship<=0 or body<=0 or site<=0 or lat<0 or lat>=36 or lon<0 or lon>=72 or        abs(x)>1000000000000 or abs(z)>1000000000000 or abs(y)>100000000 or        abs(lat_pdeg)>90000000000000 or abs(lon_pdeg)>180000000000000 or        move_tick<0 or move_tick>tick or receipt<0 or tick<0 or gen<=0:
        stop("Native planetary source/pose out of bounds")
        return
    var identity := "%d:%d:%d:%d:%d" % [ship,body,lat,lon,site]
    if _session == 0:
        if req!=0:
            stop("Unsolicited planetary movement acknowledgement")
            return
        _session=sess
        _identity=identity
    else:
        if sess!=_session or identity!=_identity or req!=_pending or            tick<=_tick or gen<=_generation or receipt<=int(_last_pose.get("receipt",0)):
            stop("Replayed or mismatched original planetary receipt")
            return
        _pending=0
    _tick=tick
    _generation=gen
    _last_pose={"ship":ship,"body":body,"lat":lat,"lon":lon,"site":site,
        "x_mm":x,"y_mm":y,"z_mm":z,"latitude_pdeg":lat_pdeg,
        "longitude_pdeg":lon_pdeg,"last_move_tick":move_tick,
        "receipt":receipt,"tick":tick,"gen":gen}
    pose_received.emit(_last_pose.duplicate(true))

func submit_walk(heading: Vector3, running: bool=false) -> void:
    if not has_verified_pose() or _pending!=0 or heading.length_squared()<0.01:
        return
    var now := Time.get_ticks_msec()
    if now-_last_send_ms < (80 if running else 140):
        return
    var v := Vector2(heading.x,heading.z).normalized()*299.0
    var dx := int(round(v.x))
    var dz := int(round(v.y))
    if dx*dx+dz*dz>300*300 or dx==0 and dz==0:
        return
    var req := _request
    _request += 1
    _pending=req
    _last_send_ms=now
    _stdout.store_line("PMOVE|%d|%d|%d|%d" % [_session,req,dx,dz])
    _stdout.flush()
