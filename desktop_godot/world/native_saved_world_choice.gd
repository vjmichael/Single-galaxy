extends RefCounted
## S6-F6: a UI-only pointer to the original user-selected SQLite world.
## This is NOT an owner credential, player state, tick, recovery receipt or
## competing simulation ledger. Every restart uses --planet-recover to verify
## actual original ownership under the OS lease before opening any writer.
const DEFAULT_HINT := "user://sg_original_world_choice.json"
const SCHEMA := "SG-S6-ORIGINAL-WORLD-CHOICE-01"
const MAX_FILE_BYTES := 2048

static func safe_path(path: String) -> bool:
    return path.is_absolute_path() and path.to_lower().ends_with(".sqlite") and \
        path.length()>0 and path.length()<=1024 and \
        not path.contains("\n") and not path.contains("\r") and \
        not path.contains("|")

static func read_hint(location: String=DEFAULT_HINT) -> Dictionary:
    if not FileAccess.file_exists(location):
        return {"status":"none"}
    var f := FileAccess.open(location,FileAccess.READ)
    if f==null:
        return {"status":"invalid"}
    if f.get_length()<=0 or f.get_length()>MAX_FILE_BYTES:
        f.close()
        return {"status":"invalid"}
    var content := f.get_as_text()
    f.close()
    var data: Variant=JSON.parse_string(content)
    if typeof(data)!=TYPE_DICTIONARY or data.get("schema","")!=SCHEMA or \
            typeof(data.get("save",null))!=TYPE_STRING or \
            not safe_path(str(data["save"])):
        return {"status":"invalid"}
    return {"status":"ok","save":str(data["save"])}

static func remember(save: String,location: String=DEFAULT_HINT) -> bool:
    if not safe_path(save):
        return false
    var f := FileAccess.open(location,FileAccess.WRITE)
    if f==null:
        return false
    f.store_string(JSON.stringify({"schema":SCHEMA,"save":save})+"\n")
    f.flush()
    var okay := f.get_error()==OK
    f.close()
    return okay

static func forget(location: String=DEFAULT_HINT) -> bool:
    if not FileAccess.file_exists(location):
        return true
    return DirAccess.remove_absolute(ProjectSettings.globalize_path(location))==OK
