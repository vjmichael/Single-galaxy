extends RefCounted
## S6-F6: preserve C++ uint64 celestial body identities EXACTLY as decimal
## strings. GDScript signed 64-bit int and JSON floating point cannot safely
## represent every original uint64. Never turn a body ID into an int/float.
const MAX_U64_DECIMAL := "18446744073709551615"

static func valid(value: Variant, zero_allowed: bool=false) -> bool:
    if typeof(value)!=TYPE_STRING:
        return false
    var digits: String=value
    if digits.is_empty() or digits.length()>20 or \
            (digits.length()>1 and digits.begins_with("0")):
        return false
    for i in range(digits.length()):
        var code := digits.unicode_at(i)
        if code<48 or code>57:
            return false
    if digits.length()==20 and digits>MAX_U64_DECIMAL:
        return false
    return zero_allowed or digits!="0"

static func same(a: Variant,b: Variant) -> bool:
    return valid(a,true) and valid(b,true) and a==b
