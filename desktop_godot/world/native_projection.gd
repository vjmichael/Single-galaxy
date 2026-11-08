extends RefCounted
## FP3D-02B.0: read-only, bounded SGDESK v1 parser.
## Only the native C++ writer owns SQLite and validates SGVIEW integrity.
## Browser, HTTP, Python and locally simulated player position are not involved.

signal snapshot_received(frame: Dictionary)
signal rejected(reason: String)

const MAX_BUFFER := 32768
const MAX_CONTACTS := 64
const MAX_LINE := 192
var _buffer := ""
var _lines: Array[String] = []
var _epoch := ""
var _seq := -1
var _tick := -1
var _rejected := false

func reset() -> void:
	_buffer = ""
	_lines.clear()
	_epoch = ""
	_seq = -1
	_tick = -1
	_rejected = false

func _fail(reason: String) -> void:
	_rejected = true
	_buffer = ""
	_lines.clear()
	rejected.emit(reason)

func _unsigned(value: String) -> bool:
	if value.is_empty() or value.length() > 19:
		return false
	if value.length() > 1 and value.begins_with("0"):
		return false
	for char_code in value.to_utf8_buffer():
		if char_code < 48 or char_code > 57:
			return false
	return value.length() != 19 or value <= "9223372036854775807"

func _epoch_uint64(value: String) -> bool:
	if value.is_empty() or value.length() > 20:
		return false
	if value.length() > 1 and value.begins_with("0"):
		return false
	for char_code in value.to_utf8_buffer():
		if char_code < 48 or char_code > 57:
			return false
	return value.length() != 20 or value <= "18446744073709551615"

func _signed(value: String) -> bool:
	if value.begins_with("-"):
		return value.length() > 1 and _unsigned(value.substr(1)) and value != "-0"
	return _unsigned(value)

func accept_bytes(data: PackedByteArray) -> void:
	if _rejected or data.is_empty():
		return
	# Strictly ASCII: no embedded NUL or noncanonical CRLF.
	for b in data:
		if b != 10 and (b < 32 or b > 126):
			_fail("non-ASCII / control character in native stream")
			return
	_buffer += data.get_string_from_ascii()
	if _buffer.length() + _lines.size() * MAX_LINE > MAX_BUFFER:
		_fail("native frame buffer exceeded limit")
		return
	while "\n" in _buffer:
		var index := _buffer.find("\n")
		var line := _buffer.substr(0, index)
		_buffer = _buffer.substr(index + 1)
		if line.length() > MAX_LINE:
			_fail("native frame line too long")
			return
		if line == "END":
			_process_packet()
			_lines.clear()
			if _rejected:
				return
		else:
			_lines.append(line)
			if _lines.size() > MAX_CONTACTS + 1:
				_fail("too many native contacts")
				return

func _process_packet() -> void:
	if _lines.is_empty():
		_fail("missing native frame header")
		return
	var header := _lines[0].split("|", false)
	if header.size() != 13 or header[0] != "SGDESK" or header[1] != "1":
		_fail("invalid native frame header/version")
		return
	for j in range(2, header.size()):
		var valid := _epoch_uint64(header[j]) if j == 2 else (_signed(header[j]) if j >= 8 and j <= 11 else _unsigned(header[j]))
		if not valid:
			_fail("invalid native frame numeric header")
			return
	var epoch := header[2]
	var seq := int(header[3])
	var tick := int(header[4])
	var system_id := int(header[5])
	var ship_id := int(header[6])
	var owner := int(header[7])
	var x := int(header[8])
	var y := int(header[9])
	var vx := int(header[10])
	var vy := int(header[11])
	var count := int(header[12])
	if owner != 7 or system_id < 1 or ship_id < 1 or count < 1 or count > MAX_CONTACTS or count != _lines.size()-1:
		_fail("invalid native ship ownership/contact count")
		return
	if _epoch.is_empty():
		if seq != 0:
			_fail("native stream must begin with a fresh snapshot")
			return
	elif epoch != _epoch or seq != _seq+1 or tick < _tick:
		_fail("native stream epoch/sequence/tick discontinuity")
		return
	var contacts: Array[Dictionary] = []
	var ids := {}
	var found_ship := false
	for i in range(1, _lines.size()):
		var parts := _lines[i].split("|", false)
		if parts.size() != 7 or parts[0] != "CONTACT":
			_fail("invalid native contact layout")
			return
		for field in parts.slice(1,4):
			if not _unsigned(field):
				_fail("invalid native contact identity")
				return
		for field in parts.slice(4,7):
			if not _signed(field):
				_fail("invalid native contact coordinate")
				return
		var id := int(parts[1])
		var kind := int(parts[2])
		var contact_owner := int(parts[3])
		if id <= 0 or kind < 0 or kind > 4 or ids.has(id):
			_fail("invalid/duplicate native contact")
			return
		ids[id] = true
		if id == ship_id:
			if kind != 0 or contact_owner != owner or int(parts[4]) != x or int(parts[5]) != y:
				_fail("native ship/contact identity mismatch")
				return
			found_ship = true
		contacts.append({"id": id, "kind": kind, "owner": contact_owner,
			"x_mm": int(parts[4]), "y_mm": int(parts[5]), "ore": int(parts[6])})
	if not found_ship:
		_fail("verified native ship absent from contacts")
		return
	_epoch = epoch; _seq = seq; _tick = tick
	snapshot_received.emit({"tick": tick, "sequence": seq, "system": system_id, "ship_id": ship_id,
		"x_mm": x, "y_mm": y, "vx_mm": vx, "vy_mm": vy, "contacts": contacts})
