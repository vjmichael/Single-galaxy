extends RefCounted
## S6-F6: read-only, short-lived SGDX-contained original-save recovery gate.
## NEVER opens a new save, guesses player custody or advances world time.
const SGDX_LAUNCH := preload("res://world/sgdx_process_launch.gd")
const SOURCE_ID := preload("res://world/native_source_id.gd")
signal decision_ready(result: Dictionary)
signal recovery_blocked(reason: String)
const MAX_LINE := 256
const MAX_PROBE_MS := 5000
var _pid := -1
var _active := false
var _stdout: FileAccess
var _stderr: FileAccess
var _buffer := PackedByteArray()
var _frame: Dictionary = {}
var _error := ""
var _save := ""
var _started_ms := 0
var _kill_issued := false

static func parse_frame(line: String) -> Dictionary:
    var f := line.split("|",false)
    if f.size()!=12 or f[0]!="RFRAME" or f[1]!="1":
        return {}
    for i in range(2,12):
        if i==5:
            if not SOURCE_ID.valid(f[i],true):
                return {}
            continue
        if f[i].is_empty() or f[i].begins_with("+") or \
                f[i].begins_with("-") or f[i].length()>19 or \
                not f[i].is_valid_int():
            return {}
    var role := int(f[2])
    var state := int(f[3])
    var ship := int(f[4])
    var body: String=f[5]
    var lat := int(f[6])
    var lon := int(f[7])
    var site := int(f[8])
    var phase := int(f[9])
    var tick := int(f[10])
    var gen := int(f[11])
    if role not in [1,2,3,4] or tick<0 or gen<=0 or \
            ship<0 or lat<0 or lat>=36 or \
            lon<0 or lon>=72 or site<0 or phase not in [0,1,2,3]:
        return {}
    if role==1 and (state!=4 or ship!=0 or body!="0" or lat!=0 or lon!=0 or site!=0 or phase!=0):
        return {}
    if role==2 and (state!=0 or ship==0 or body!="0" or lat!=0 or lon!=0 or site!=0 or phase!=0):
        return {}
    if role==3 and (state not in [5,6] or ship==0 or body=="0" or \
            site==0 or (state==5 and phase not in [0,2]) or \
            (state==6 and phase not in [1,3])):
        return {}
    if role==4 and (state!=7 or ship==0 or body==0 or site==0 or phase!=2):
        return {}
    return {"role":role,"state":state,"ship":ship,"body":body,
        "lat":lat,"lon":lon,"site":site,"phase":phase,
        "tick":tick,"gen":gen}

func is_active() -> bool:
    return _active

func process_id() -> int:
    return _pid if _active and _pid>0 else -1

func start(save: String) -> bool:
    if _active:
        recovery_blocked.emit("Recovery probe already running")
        return false
    if not save.is_absolute_path() or not save.to_lower().ends_with(".sqlite") or \
            not FileAccess.file_exists(save):
        recovery_blocked.emit("Original saved world is missing; refusing recovery or genesis")
        return false
    var binary := "galaxy_desktop_player_host.exe" if OS.get_name()=="Windows" else "galaxy_desktop_player_host"
    var host := SGDX_LAUNCH.native_host_path(binary)
    var spec := SGDX_LAUNCH.supervised_command(save,"godot_planet_recovery",
        host,PackedStringArray([save,"--planet-recover"]))
    if spec.is_empty():
        recovery_blocked.emit("Native recovery host or SGDX guardian missing")
        return false
    var proc := OS.execute_with_pipe(spec["path"],spec["args"],false)
    if not proc.has("pid") or not proc.has("stdio"):
        recovery_blocked.emit("Native recovery preflight did not launch")
        return false
    _pid=int(proc["pid"])
    _stdout=proc["stdio"]
    _stderr=proc.get("stderr")
    _save=save
    _frame.clear()
    _buffer.clear()
    _error=""
    _active=true
    _started_ms=Time.get_ticks_msec()
    _kill_issued=false
    return true

func poll() -> void:
    if not _active:
        return
    for _i in range(12):
        var chunk := _stdout.get_buffer(512)
        if chunk.is_empty():
            break
        for byte in chunk:
            if byte==10:
                var line := _buffer.get_string_from_ascii()
                _buffer.clear()
                if not _frame.is_empty():
                    _error="Repeated original recovery decision"
                else:
                    _frame=parse_frame(line)
                    if _frame.is_empty():
                        _error="Untrusted native recovery packet"
            elif byte==13:
                continue
            elif byte<32 or byte>126 or _buffer.size()>=MAX_LINE:
                _error="Malformed native recovery packet"
            else:
                _buffer.append(byte)
    # Do not admit the next writer merely because RFRAME appeared on stdout.
    # The SGDX parent and its OS-custodied child must BOTH have exited.
    if _pid>0 and OS.is_process_running(_pid):
        if Time.get_ticks_msec()-_started_ms>MAX_PROBE_MS:
            if not _kill_issued:
                _error="Original recovery probe exceeded native timeout; writer remains locked until OS exit"
                _kill_issued=true
                recovery_blocked.emit(_error)
                OS.kill(_pid) # one guardian termination; no replacement writer yet
        return
    var valid := not _frame.is_empty() and _error.is_empty() and _buffer.is_empty()
    var decision := _frame.duplicate(true)
    decision["save"]=_save
    _active=false
    _pid=-1
    _stdout=null
    _stderr=null
    _save=""
    _frame.clear()
    _buffer.clear()
    if valid:
        decision_ready.emit(decision)
    else:
        recovery_blocked.emit(
            _error if not _error.is_empty() else
            "Original recovery process failed or returned no committed custody decision")

func stop(reason: String="Original recovery preflight cancelled") -> void:
    if _active and _pid>0 and OS.is_process_running(_pid):
        OS.kill(_pid)
    _active=false
    _pid=-1
    _stdout=null
    _stderr=null
    _buffer.clear()
    _frame.clear()
    _save=""
    _error=reason
