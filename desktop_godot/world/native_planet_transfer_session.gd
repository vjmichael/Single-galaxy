extends RefCounted
## S6-F5-F: read and propose owner actions against the ORIGINAL native
## PlayerPerson and planetary shuttle. No local flight, state mutations, or
## alternate save. This role excludes planet and cockpit writers.
const SGDX_LAUNCH := preload("res://world/sgdx_process_launch.gd")
const SOURCE_ID := preload("res://world/native_source_id.gd")
signal state_received(state: Dictionary)
signal status_changed(message: String)
signal landed()
signal returned_to_ship()
signal connection_changed(connected: bool)
const MAX_LINE := 768
const CLOCK_TIMEOUT_MS := 3000
var _active := false
var _pid := -1
var _last_pid := -1
var _stdin: FileAccess
var _stderr: FileAccess
var _buffer := PackedByteArray()
var _session := 0
var _next_request := 1
var _pending := 0
var _pending_action := ""
var _last_sent_ms := 0
var _last_frame_ms := 0
var _gen := -1
var _tick := -1
var _ship := 0
var _last_action_receipt := 0
var _sites: Array[Dictionary] = []
var _state: Dictionary = {}

func is_active() -> bool:
    return _active

func process_id() -> int:
    if _active:
        return _pid
    if _last_pid > 0 and OS.is_process_running(_last_pid):
        return _last_pid
    _last_pid=-1
    return -1

func has_verified_state() -> bool:
    return _active and _session>0 and not _state.is_empty()

func current_state() -> Dictionary:
    return _state.duplicate(true) if has_verified_state() else {}

func sites() -> Array[Dictionary]:
    return _sites.duplicate(true) if has_verified_state() else []

func start(save: String) -> void:
    if is_active() or process_id()>0:
        status_changed.emit("Previous planetary writer still alive; no second writer")
        return
    if not save.is_absolute_path() or not save.to_lower().ends_with(".sqlite") or \
            not FileAccess.file_exists(save):
        status_changed.emit("Planetary transfer requires an existing original SQLite save")
        return
    var host_name := "galaxy_desktop_player_host.exe" if OS.get_name()=="Windows" else "galaxy_desktop_player_host"
    var host := SGDX_LAUNCH.native_host_path(host_name)
    var spec := SGDX_LAUNCH.supervised_command(save,"godot_planet_transfer",
        host,PackedStringArray([save,"--planet-transfer"]))
    if spec.is_empty():
        status_changed.emit("Original installed SGDX/native shuttle host missing")
        return
    var result := OS.execute_with_pipe(spec["path"],spec["args"],false)
    if not result.has("pid") or not result.has("stdio"):
        status_changed.emit("Original shuttle transfer process refused to start")
        return
    _pid=int(result["pid"])
    _stdin=result["stdio"]
    _stderr=result.get("stderr")
    _active=true
    _session=0
    _next_request=1
    _pending=0
    _pending_action=""
    _last_sent_ms=0
    _last_frame_ms=Time.get_ticks_msec()
    _gen=-1
    _tick=-1
    _ship=0
    _last_action_receipt=0
    _buffer.clear()
    _sites.clear()
    _state.clear()
    connection_changed.emit(true)
    status_changed.emit("Verifying original owner, ship, shuttle, planet and SQLite custody")

func stop(reason: String="Original shuttle transfer closed",graceful: bool=false) -> void:
    if _pid>0:
        _last_pid=_pid
    if _active and _pid>0 and OS.is_process_running(_pid):
        if graceful and _stdin!=null:
            _stdin.store_line("QUIT")
            _stdin.flush()
        else:
            OS.kill(_pid)
    _active=false
    _pid=-1
    _stdin=null
    _stderr=null
    _pending=0
    _pending_action=""
    _session=0
    _state.clear()
    _sites.clear()
    _buffer.clear()
    connection_changed.emit(false)
    status_changed.emit(reason)

func poll() -> void:
    if not _active:
        return
    for _n in range(16):
        var data := _stdin.get_buffer(4096)
        if data.is_empty():
            break
        for b in data:
            if b==10:
                var line := _buffer.get_string_from_ascii()
                _buffer.clear()
                _accept(line)
            elif b==13:
                continue
            elif b<32 or b>126 or _buffer.size()>=MAX_LINE:
                stop("Original shuttle protocol invalid; recovery requires SQLite reload")
                return
            else:
                _buffer.append(b)
        if not _active:
            return
    if _pending>0 and Time.get_ticks_msec()-_last_sent_ms>CLOCK_TIMEOUT_MS:
        stop("Original shuttle operation timed out; receipt uncertain, reconnect from saved world")
        return
    if Time.get_ticks_msec()-_last_frame_ms>CLOCK_TIMEOUT_MS:
        stop("Original shuttle clock stalled; native writer recovery required")
        return
    if not OS.is_process_running(_pid):
        stop("Original shuttle host exited; reconnect to recover saved state")

func _valid(s: String) -> bool:
    return not s.is_empty() and s.length()<=20 and s.is_valid_int() and not s.begins_with("+")

func _accept(line: String) -> void:
    var parts := line.split("|",false)
    if parts.size()==4 and parts[0]=="DENY" and parts[1]=="1" and _valid(parts[2]):
        var request := int(parts[2])
        if request==_pending:
            _pending=0
            _pending_action=""
        status_changed.emit("Original shuttle refused action #%d: %s" % [request,parts[3]])
        return
    if parts.size()==9 and parts[0]=="TSITE" and parts[1]=="1":
        for i in range(2,9):
            if i==4:
                if not SOURCE_ID.valid(parts[i]):
                    stop("Invalid original uint64 shuttle body")
                    return
                continue
            if not _valid(parts[i]):
                stop("Invalid original shuttle source-site record")
                return
        var ses := int(parts[2])
        var idx := int(parts[3])
        var body: String=parts[4]
        var lat := int(parts[5])
        var lon := int(parts[6])
        var surface := int(parts[7])
        var phase := int(parts[8])
        if ses<=0 or (_session>0 and ses!=_session) or _sites.size()>=16 or \
                idx!=_sites.size() or lat<0 or lat>=36 or \
                lon<0 or lon>=72 or surface<=0 or phase not in [0,1,2,3]:
            stop("Mismatched original shuttle site list")
            return
        _sites.append({"body":body,"lat":lat,"lon":lon,"site":surface,"phase":phase})
        return
    if parts.size()==19 and parts[0]=="TFRAME" and parts[1]=="1":
        for i in range(2,19):
            if i==6:
                if not SOURCE_ID.valid(parts[i]):
                    stop("Invalid original uint64 shuttle body")
                    return
                continue
            if not _valid(parts[i]):
                stop("Invalid original shuttle state number")
                return
        var ses := int(parts[2])
        var request := int(parts[3])
        var ship := int(parts[4])
        var person_state := int(parts[5])
        var body: String=parts[6]
        var lat := int(parts[7])
        var lon := int(parts[8])
        var phase := int(parts[9])
        var tick := int(parts[10])
        var gen := int(parts[11])
        var site := int(parts[12])
        var flight := int(parts[13])
        var descent := int(parts[14])
        var ascent := int(parts[15])
        var receipt := int(parts[16])
        var selected := int(parts[17])
        var count := int(parts[18])
        if ses<=0 or ship<=0 or person_state not in [0,5,6] or \
                lat<0 or lat>=36 or lon<0 or lon>=72 or \
                phase not in [0,1,2,3] or tick<0 or gen<=0 or site<=0 or \
                flight<0 or descent<0 or ascent<0 or receipt<0 or \
                count<=0 or count>16 or count!=_sites.size() or \
                selected<0 or selected>=count:
            stop("Impossible original shuttle state")
            return
        var o: Dictionary=_sites[selected]
        if body!=str(o["body"]) or lat!=int(o["lat"]) or lon!=int(o["lon"]) or \
                site!=int(o["site"]):
            stop("Original shuttle source changed or site packet was forged")
            return
        if _session==0:
            if request!=0 or receipt!=0:
                stop("Unsolicited original shuttle action")
                return
            _session=ses
            _ship=ship
        else:
            if ses!=_session or ship!=_ship or tick<_tick or gen<_gen:
                stop("Regressed original shuttle generation or identity")
                return
            if request==0:
                if receipt!=0 or tick<=_tick or gen<=_gen:
                    stop("Invalid unsolicited original shuttle tick")
                    return
            else:
                if request!=_pending:
                    stop("Replayed or mismatched original shuttle command receipt")
                    return
                if _pending_action=="SELECT":
                    if receipt!=0:
                        stop("Site selection incorrectly mutated original world")
                        return
                elif receipt<=_last_action_receipt or gen<=_gen:
                    stop("Missing original shuttle transaction receipt")
                    return
                if receipt>0:
                    _last_action_receipt=receipt
                _pending=0
                _pending_action=""
        _last_frame_ms=Time.get_ticks_msec()
        _tick=tick
        _gen=gen
        _state={"session":ses,"ship":ship,"state":person_state,
            "body":body,"lat":lat,"lon":lon,"phase":phase,"tick":tick,
            "gen":gen,"site":site,"flight":flight,"descent":descent,
            "ascent":ascent,"receipt":receipt,"selected":selected,
            "site_count":count}
        state_received.emit(_state.duplicate(true))
        return
    if parts.size() in [8,9] and parts[1]=="1" and \
            parts[0] in ["TLAND","TRETURN"]:
        var is_landing: bool=parts[0]=="TLAND"
        if (is_landing and parts.size()!=9) or (not is_landing and parts.size()!=8):
            stop("Invalid original shuttle handoff length")
            return
        for i in range(2,parts.size()):
            if is_landing and i==5:
                if not SOURCE_ID.valid(parts[i]):
                    stop("Invalid original uint64 landing body")
                    return
                continue
            if not _valid(parts[i]):
                stop("Invalid native shuttle handoff receipt")
                return
        if int(parts[2])!=_session or int(parts[3])!=_pending or \
                int(parts[4])!=_ship or \
                _pending_action!=("DISEMBARK" if is_landing else "EXIT"):
            stop("Unrequested shuttle-to-player handoff")
            return
        if is_landing and not SOURCE_ID.same(parts[5],_state.get("body","0")):
            stop("Handoff planet differs from original selected site")
            return
        var tick_idx := 6 if is_landing else 5
        var generation_idx := 7 if is_landing else 6
        var receipt_idx := 8 if is_landing else 7
        if int(parts[tick_idx])<_tick or int(parts[generation_idx])<=_gen or \
                int(parts[receipt_idx])<=_last_action_receipt:
            stop("Original shuttle handoff journal evidence regressed")
            return
        _last_action_receipt=int(parts[receipt_idx])
        _pending=0
        _pending_action=""
        _last_frame_ms=Time.get_ticks_msec()
        if is_landing:
            landed.emit()
        else:
            returned_to_ship.emit()
        return
    stop("Unexpected original shuttle protocol data")

func submit_action(action: String) -> bool:
    if not has_verified_state() or _pending!=0 or \
            action not in ["BOARD","DESCEND","DISEMBARK","ASCEND","EXIT"]:
        return false
    return _send("TDO",action)

func select_site(index: int) -> bool:
    if not has_verified_state() or _pending!=0 or \
            int(_state.get("state",-1))!=0 or index<0 or index>=_sites.size():
        return false
    return _send("TSELECT",str(index),"SELECT")

func can_abort_to_ship() -> bool:
    return has_verified_state() and _pending==0 and \
        int(_state.get("state",-1))==0

func _send(verb: String,arg: String,kind: String="") -> bool:
    var ms := Time.get_ticks_msec()
    if ms-_last_sent_ms<130:
        return false
    var request := _next_request
    _next_request+=1
    _pending=request
    _pending_action=kind if not kind.is_empty() else arg
    _last_sent_ms=ms
    _stdin.store_line("%s|%d|%d|%s" % [verb,_session,request,arg])
    _stdin.flush()
    return true
