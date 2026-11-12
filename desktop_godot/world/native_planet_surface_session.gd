extends RefCounted
## S6-F5-C: supervised original-world planet mode of the installed C++ host.
## The original player, source body, altitude, receipt, and SQLite generation
## are supplied ONLY by the verified C++ PFRAME. No Godot collision or local
## CharacterBody movement is ever submitted as authoritative.
const SGDX_LAUNCH := preload("res://world/sgdx_process_launch.gd")
const SOURCE_ID := preload("res://world/native_source_id.gd")
signal pose_received(pose: Dictionary)
signal status_changed(message: String)
signal connection_changed(connected: bool)
signal reboard_completed()
signal npc_pose_received(npc_id: int, pose: Dictionary)
signal npc_roster_received(ids: Array)
const MAX_LINE := 1024
var _active := false
var _pid := -1
var _last_shutdown_pid := -1
var _stdout: FileAccess
var _stderr: FileAccess
var _buffer := PackedByteArray()
var _session := 0
var _request := 1
var _pending := 0
var _pending_kind := ""
var _last_send_ms := -100000
var _last_frame_ms := 0
const MAX_IDLE_PFRAME_MS := 3000
var _tick := -1
var _generation := -1
var _identity := ""
var _last_pose: Dictionary = {}
var _last_action_receipt := 0
# S6-F5-D: no Godot NPC state is authoritative. Only native roster/NACK.
var _npc_roster: Array[int] = []
var _npc_poses: Dictionary = {}
var _last_npc_receipt := 0

func is_active() -> bool:
    return _active

func has_verified_pose() -> bool:
    return _active and _session > 0 and not _identity.is_empty()

func verified_npcs() -> Array:
    return _npc_roster.duplicate() if has_verified_pose() else []

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
    _pending_kind = ""
    _tick = -1
    _generation = -1
    _identity = ""
    _last_pose.clear()
    _last_action_receipt = 0
    _last_npc_receipt = 0
    _npc_roster.clear()
    _npc_poses.clear()
    _last_frame_ms = Time.get_ticks_msec()
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
    _pending_kind = ""
    _identity = ""
    _last_pose.clear()
    _npc_roster.clear()
    _npc_poses.clear()
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
    # Background world STEP must produce continuous original PFRAME proof even
    # with zero Godot input. A live-but-stalled writer is not a valid clock.
    # Keep the displayed character locked until the original SQLite reload.
    if Time.get_ticks_msec() - _last_frame_ms > MAX_IDLE_PFRAME_MS:
        stop("Original planetary clock stalled; SQLite recovery required")
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
            _pending_kind = ""
        status_changed.emit("Original C++ refused planetary step #%d: %s" % [id, p[3]])
        return
    if p.size() >= 5 and p[0] == "NROSTER" and p[1] == "1":
        if not has_verified_pose() or not _integer(p[2]) or                 not _integer(p[3]) or not _integer(p[4]):
            stop("Native NPC roster lacks original site custody")
            return
        var n := int(p[4])
        if int(p[2]) != _session or n < 0 or n > 16 or                 p.size() != 5+n or int(p[3]) < _generation:
            stop("Original NPC roster generation mismatch")
            return
        var ids: Array[int] = []
        for i in range(5,p.size()):
            if not _integer(p[i]) or int(p[i]) <= 0 or                     (not ids.is_empty() and int(p[i]) <= ids.back()):
                stop("Original native NPC roster malformed")
                return
            ids.append(int(p[i]))
        _npc_roster = ids
        _npc_poses.clear()
        npc_roster_received.emit(ids.duplicate())
        return
    if p.size() == 10 and p[0] == "NACK" and p[1] == "1":
        for i in range(2,10):
            if not _integer(p[i]):
                stop("Malformed original NPC motion receipt")
                return
        var npc := int(p[4])
        if not has_verified_pose() or _pending_kind != "NMOVE" or                 int(p[2]) != _session or int(p[3]) != _pending or                 not _npc_roster.has(npc) or int(p[7]) < _tick or                 int(p[8]) <= _generation or int(p[9]) <= _last_npc_receipt or                 abs(int(p[5])) > 1000000000000 or                 abs(int(p[6])) > 1000000000000:
            stop("Original NPC motion identity/receipt mismatch")
            return
        _npc_poses[npc] = {"npc": npc, "x_mm": int(p[5]),
            "z_mm": int(p[6]), "tick": int(p[7]),
            "generation": int(p[8]), "receipt": int(p[9])}
        _last_npc_receipt = int(p[9])
        _pending = 0
        _pending_kind = ""
        npc_pose_received.emit(npc, _npc_poses[npc].duplicate(true))
        return
    # This is a DIFFERENT receipt from a planetary PFRAME: the original
    # player is no longer ashore. Never fabricate a final surface pose.
    if p.size()==11 and p[0]=="PLEAVE" and p[1]=="1":
        for i in range(2,11):
            if i==5:
                if not SOURCE_ID.valid(p[i]):
                    stop("Invalid original unsigned planetary body on reboard")
                    return
                continue
            if not _integer(p[i]):
                stop("Malformed original shuttle reboarding receipt")
                return
        if not has_verified_pose() or _pending_kind!="PREBOARD" or                 int(p[2])!=_session or int(p[3])!=_pending or                 int(p[4])!=int(_last_pose.get("ship",0)) or                 not SOURCE_ID.same(p[5],_last_pose.get("body","0")) or                 int(p[6])!=int(_last_pose.get("lat",-1)) or                 int(p[7])!=int(_last_pose.get("lon",-1)) or                 int(p[8])<_tick or int(p[9])<=_generation or                 int(p[10])<=_last_action_receipt:
            stop("Original reboarding source/epoch mismatch")
            return
        _last_action_receipt=int(p[10])
        _pending=0
        _pending_kind=""
        reboard_completed.emit()
        return
    if p.size() != 40 or p[0]!="PFRAME" or p[1]!="1":
        stop("Unexpected original planetary host frame")
        return
    for i in range(2,40):
        if i==5:
            if not SOURCE_ID.valid(p[i]):
                stop("Invalid original unsigned planetary body")
                return
            continue
        if not _integer(p[i]):
            stop("Non-numeric original planetary pose")
            return
    var sess := int(p[2])
    var req := int(p[3])
    var ship := int(p[4])
    var body: String=p[5]
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
    var gravity := int(p[18])
    var radial_speed := int(p[19])
    var grounded := int(p[20])
    var oxygen_kg := int(p[21])
    var co2_kg := int(p[22])
    var thermal_ms := int(p[23])
    var air_unknown_ms := int(p[24])
    var injury := int(p[25])
    var vertical_tick := int(p[26])
    var command_receipt := int(p[27])
    var pressure := int(p[28])
    var oxygen_permille := int(p[29])
    var toxic := int(p[30])
    var atmosphere_known := int(p[31])
    var breathable := int(p[32])
    var suit_mass := int(p[33])
    var suit_condition := int(p[34])
    var suit_charge := int(p[35])
    var suit_equipped := int(p[36])
    var hypoxia_ms := int(p[37])
    var care_count := int(p[38])
    var last_care_tick := int(p[39])
    if sess<=0 or ship<=0 or site<=0 or lat<0 or lat>=36 or lon<0 or lon>=72 or        abs(x)>1000000000000 or abs(z)>1000000000000 or abs(y)>100000000 or        abs(lat_pdeg)>90000000000000 or abs(lon_pdeg)>180000000000000 or        move_tick<0 or move_tick>tick or receipt<0 or tick<0 or gen<=0 or \
       gravity<0 or gravity>50000 or abs(radial_speed)>60000 or \
       grounded not in [0,1] or oxygen_kg<0 or oxygen_kg>4 or co2_kg<0 or co2_kg>4 or \
       oxygen_kg+co2_kg>4 or thermal_ms<0 or thermal_ms>1000000000000 or \
       air_unknown_ms<0 or air_unknown_ms>1000000000000 or injury<0 or injury>100 or \
       vertical_tick<0 or vertical_tick>tick or command_receipt<0 or \
       pressure<0 or pressure>500000 or oxygen_permille<0 or oxygen_permille>1000 or \
       toxic not in [0,1] or atmosphere_known not in [0,1] or breathable not in [0,1] or \
       (breathable==1 and atmosphere_known==0) or \
       (atmosphere_known==0 and (pressure!=0 or oxygen_permille!=0 or toxic!=0)) or \
       suit_mass not in [0,10] or suit_condition<0 or suit_condition>100 or \
       suit_charge<0 or suit_charge>10 or suit_equipped not in [0,1] or \
       (suit_mass==0 and (suit_condition!=0 or suit_charge!=0 or suit_equipped==1)) or \
       hypoxia_ms<0 or hypoxia_ms>1000000000000 or \
       care_count<0 or care_count>100000 or last_care_tick<0 or last_care_tick>tick:
        stop("Native planetary source/pose out of bounds")
        return
    var identity := "%d:%s:%d:%d:%d" % [ship,body,lat,lon,site]
    if _session == 0:
        if req!=0 or command_receipt!=0:
            stop("Unsolicited planetary movement acknowledgement")
            return
        _session=sess
        _identity=identity
    else:
        if sess!=_session or identity!=_identity or gen<=_generation or \
           tick<_tick or vertical_tick<int(_last_pose.get("last_vertical_tick",0)):
            stop("Regressed or mismatched original planetary identity")
            return
        if req==0:
            # Native clock-driven original world step. No human command was
            # issued; this is fresh source evidence, not a movement ACK.
            if command_receipt!=0 or tick<=_tick or \
               receipt!=int(_last_pose.get("receipt",0)):
                stop("Invalid background survival tick")
                return
        else:
            if req!=_pending or command_receipt<=_last_action_receipt or \
               receipt<int(_last_pose.get("receipt",0)):
                stop("Replayed or mismatched original planetary action receipt")
                return
            if _pending_kind=="PREBOARD":
                stop("Surface frame cannot acknowledge original shuttle reboarding")
                return
            _last_action_receipt=command_receipt
            _pending=0
            _pending_kind=""
    _tick=tick
    _generation=gen
    _last_frame_ms=Time.get_ticks_msec()
    _last_pose={"session":sess,"ship":ship,"body":body,"lat":lat,"lon":lon,"site":site,
        "x_mm":x,"y_mm":y,"z_mm":z,"latitude_pdeg":lat_pdeg,
        "longitude_pdeg":lon_pdeg,"last_move_tick":move_tick,
        "receipt":receipt,"tick":tick,"gen":gen,
        "gravity_mm_s2":gravity,"radial_speed_mm_s":radial_speed,
        "grounded":grounded==1,"oxygen_kg":oxygen_kg,"co2_kg":co2_kg,
        "thermal_ms":thermal_ms,"air_uncertain_ms":air_unknown_ms,
        "injury_points":injury,"last_vertical_tick":vertical_tick,
        "command_receipt":command_receipt,
        "atmosphere_known":atmosphere_known==1,
        "pressure_pa":pressure,"oxygen_permille":oxygen_permille,
        "atmosphere_toxic":toxic==1,"ambient_breathable":breathable==1,
        "suit_mass_kg":suit_mass,"suit_integrity":suit_condition,
        "suit_charge":suit_charge,"suit_equipped":suit_equipped==1,
        "hypoxia_ms":hypoxia_ms,"medical_treatments":care_count,
        "last_medical_tick":last_care_tick}
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
    _pending_kind="PMOVE"
    _last_send_ms=now
    _stdout.store_line("PMOVE|%d|%d|%d|%d" % [_session,req,dx,dz])
    _stdout.flush()

func submit_npc_move(npc_id: int, dx: int, dz: int) -> bool:
    if not has_verified_pose() or _pending != 0 or             not _npc_roster.has(npc_id):
        return false
    if abs(dx) > 300 or abs(dz) > 300 or abs(dx)+abs(dz) > 300:
        return false
    var now := Time.get_ticks_msec()
    if now-_last_send_ms < 100:
        return false
    var req := _request
    _request += 1
    _pending = req
    _pending_kind = "NMOVE"
    _last_send_ms = now
    _stdout.store_line("NMOVE|%d|%d|%d|%d|%d" %
        [_session, req, npc_id, dx, dz])
    _stdout.flush()
    return true

func submit_vertical(jump: bool) -> void:
    _send_player_action("PSTEP", "JUMP" if jump else "WAIT")

func submit_oxygen(return_to_site: bool) -> void:
    _send_player_action("POXY", "RETURN" if return_to_site else "CHARGE")

func _send_player_action(verb: String, mode: String) -> void:
    if not has_verified_pose() or _pending!=0:
        return
    var now := Time.get_ticks_msec()
    if now-_last_send_ms<100:
        return
    var req := _request
    _request += 1
    _pending=req
    _pending_kind=verb
    _last_send_ms=now
    _stdout.store_line("%s|%d|%d|%s" % [verb,_session,req,mode])
    _stdout.flush()

func suit_equipped() -> bool:
    return has_verified_pose() and bool(_last_pose.get("suit_equipped",false))

func submit_suit(action: String) -> void:
    if action not in ["EQUIP", "UNEQUIP", "CHARGE"]:
        return
    _send_player_action("PSUIT", action)

func submit_medical() -> void:
    _send_player_action("PMED", "TREAT")

func submit_reboard() -> void:
    # The native host checks the original hatch, grounded state and returned
    # oxygen/CO2, then performs the original SQLite Reboard transaction.
    _send_player_action("PREBOARD", "REBOARD")
