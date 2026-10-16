$include <stdio.hsh>
$include <stdlib.hsh>
$include <string.hsh>

new global_calls as i32 = 0
new cached_calls as i32 = 0
new global_text[4] as cstr = global_source()
func global_source() -> [8] as cstr { global_calls++; return "global"; }
func cached_source() -> [8] as cstr { cached_calls++; return "cached"; }
func cached() -> [8] as cstr { static saved[8] as cstr = cached_source(); return saved; }

new calls as i32 = 0
func text() -> [8] as cstr { calls++; return "hello"; }
func other() -> out[8] as cstr { calls++; return "world"; }
func relay() -> out[4] as cstr { return text(); }
func anonymous_short() -> [3] as cstr { return text(); }
func anonymous_wide() -> [12] as cstr { return text(); }
func pair() -> (first[8] as cstr, second[4] as cstr) {
    calls++
    return "abcdef", "xy"
}
func anonymous_pair() -> ([8] as cstr, i32) { return "mixed", 42; }
func raw_pair() -> (raw as f64, code as i32) {
    raw %s= "float"
    code = 7
    return
}

template Text { head[1] as u8; value[8] as cstr; tail[1] as u8; }
template Raw { value[8] as bytes; }

new adjusted_assignments as i32 = 0
template Adjusted { value[4] as i32; }
impl Adjusted {
    op = (target as Adjusted, source as Adjusted) -> Adjusted {
        adjusted_assignments++
        target.value = source.value + 1
        return target
    }
    func label(self as Adjusted) -> [8] as cstr { return "method"; }
    op + (left as Adjusted, right as Adjusted) -> [8] as cstr { return "operator"; }
}
func mixed_siblings(value as Adjusted, floating as f64, raw[4] as bytes, byte as u8) -> ([8] as cstr, bool, [4] as bytes, f32, u64, Adjusted) {
    return "mixed", raw, raw, floating, byte, value
}

func main() -> i32 {
    new failures as i32 = 0
    if (strcmp(global_text, "glo") != 0 || global_calls != 1) { failures++; }
    if (strcmp(cached(), "cached") != 0 || strcmp(cached(), "cached") != 0 || cached_calls != 1) { failures++; }
    new source[8] as cstr = "hello"
    new padded[12] as cstr = source
    new short[4] as cstr = source
    new equal[8] as cstr = source
    new one[1] as cstr = source
    if (strcmp(padded, "hello") != 0 || strcmp(short, "hel") != 0) { failures++; }
    if (strcmp(equal, "hello") != 0 || one[0] != 0) { failures++; }
    new index as i32 = 5
    while (index < 12) {
        if (padded[index] != 0) { failures++; }
        index++
    }
    new literal_short[4] as cstr = "abcdef"
    new literal_equal[4] as cstr = "abc"
    new literal_one[1] as cstr = "a"
    if (strcmp(literal_short, "abc") != 0 || strcmp(literal_equal, "abc") != 0 || literal_one[0] != 0) { failures++; }

    new called[4] as cstr = text()
    if (strcmp(called, "hel") != 0 || calls != 1) { failures++; }
    called = false ? text() : other()
    if (strcmp(called, "wor") != 0 || calls != 2) { failures++; }
    called = true ? text() : other()
    if (strcmp(called, "hel") != 0 || calls != 3) { failures++; }
    new inferred as cstr = other()
    if (length(inferred) != 8 || strcmp(inferred, "world") != 0 || calls != 4) { failures++; }
    called = relay()
    if (strcmp(called, "hel") != 0 || calls != 5) { failures++; }
    called = anonymous_short()
    if (strcmp(called, "he") != 0 || calls != 6) { failures++; }
    padded = anonymous_wide()
    if (strcmp(padded, "hello") != 0 || calls != 7) { failures++; }

    new raw[8] as bytes = "ab\0cdef"
    padded = raw as cstr
    if (strcmp(padded, "ab") != 0) { failures++; }
    index = 2
    while (index < 12) {
        if (padded[index] != 0) { failures++; }
        index++
    }
    if (raw[3] != 'c' || raw[6] != 'f') { failures++; }
    called = (source = "next")
    if (strcmp(called, "nex") != 0 || strcmp(source, "next") != 0) { failures++; }
    if (length(called = text()) != 4 || calls != 8) { failures++; }
    new snapshot[4] as cstr = (called = other())
    called = "new"
    if (strcmp(snapshot, "wor") != 0 || calls != 9) { failures++; }

    new framed as Text
    framed.head = 17
    framed.tail = 19
    framed.value = text()
    if (strcmp(framed.value, "hello") != 0 || framed.head != 17 || framed.tail != 19 || calls != 10) { failures++; }
    new buffer[12] as bytes = "..........."
    (buffer as cstr) = source
    if (buffer[0] != 'n' || buffer[4] != 0 || buffer[11] != 0) { failures++; }

    called, padded = pair()
    if (strcmp(called, "abc") != 0 || strcmp(padded, "xy") != 0 || calls != 11) { failures++; }
    called, called = pair()
    if (strcmp(called, "xy") != 0 || calls != 12) { failures++; }
    new code as i32
    padded, code = anonymous_pair()
    if (strcmp(padded, "mixed") != 0 || code != 42) { failures++; }

    new integer as u64 %s= "integer"
    new floating as f64 %s= "float"
    new plain[8] %s= "raw"
    new bytes[8] as bytes %s= "bytes"
    new object as Raw
    object.value %s= "object"
    padded %s= integer
    if (strcmp(padded, "integer") != 0) { failures++; }
    padded %s= floating
    if (strcmp(padded, "float") != 0) { failures++; }
    padded %s= plain
    if (strcmp(padded, "raw") != 0) { failures++; }
    padded %s= bytes
    if (strcmp(padded, "bytes") != 0) { failures++; }
    padded %s= object
    if (strcmp(padded, "object") != 0) { failures++; }
    (padded %s=), code = raw_pair()
    if (strcmp(padded, "float") != 0 || code != 7) { failures++; }
    new adjusted as Adjusted
    adjusted.value = 40
    new adjusted_result as Adjusted
    new truth as bool
    new raw_sibling[4] as bytes = "raw"
    new raw_result[4] as bytes
    new float_result as f64
    new wide as u64
    padded, truth, raw_result, float_result, wide, adjusted_result = mixed_siblings(adjusted, 3.5, raw_sibling, 255)
    if (strcmp(padded, "mixed") != 0 || !truth || memcmp(raw_result, raw_sibling, 4) != 0) { failures++; }
    if (float_result != 3.5 || wide != 255 || adjusted_result.value != 42 || adjusted_assignments != 2) { failures++; }
    if (adjusted.value != 40) { failures++; }
    padded = adjusted.label()
    if (strcmp(padded, "method") != 0) { failures++; }
    padded = adjusted + adjusted
    if (strcmp(padded, "operato") != 0) { failures++; }
    printf("cstr assignments %d\n", failures)
    return failures
}
