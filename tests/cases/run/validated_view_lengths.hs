$include <stdio.hsh>
$include <stdlib.hsh>

new calls as u64 = 0
new init_order as u64 = 0
// Forward function/global references affect extent resolution, not execution.
new global_length = length(later)
new first_call = advance()
new second_length = length(advance())
new global_flag = true
new later as bytes = 'abc'
new mutual_first = length(mutual_second)
new mutual_second = length(mutual_first)

func advance() -> u64 {
    init_order = init_order + 1
    return init_order
}
func tick() -> u64 {
    calls = calls + 1
    return 0x0102030405060708
}
func width() -> u64 {
    calls = calls + 1
    return 3
}
func float_tick() -> f64 {
    calls = calls + 1
    return 1.0
}
func once() -> u64 {
    static held = tick()
    return held
}
func self_length() -> u64 {
    new count = length(count)
    return count
}
func shadow_length() -> u64 {
    new value[1] = 0
    if (true) { new value = length(value); return value; }
    return 0
}
// Integrated contract: validated conversion reads an assignment rvalue snapshot.
func assignment_f16(value as f16) -> i32 {
    new expected as f16 = 2.5
    new converted = to_i32(value = expected)
    if (converted != 2 || value != expected) { return 1; }
    expected = 3.0
    new measured = length(value = expected)
    if (measured != 2 || value != expected) { return 1; }
    return 0
}
// Integrated contract: validated conversion reads an assignment rvalue snapshot.
func assignment_f32(value as f32) -> i32 {
    new expected as f32 = 2.5
    new converted = to_i32(value = expected)
    if (converted != 2 || value != expected) { return 1; }
    expected = 3.0
    new measured = length(value = expected)
    if (measured != 4 || value != expected) { return 1; }
    return 0
}
// Integrated contract: validated conversion reads an assignment rvalue snapshot.
func assignment_f64(value as f64) -> i32 {
    new expected as f64 = 2.5
    new converted = to_i32(value = expected)
    if (converted != 2 || value != expected) { return 1; }
    expected = 3.0
    new measured = length(value = expected)
    if (measured != 8 || value != expected) { return 1; }
    return 0
}
// Integrated contract: validated conversion reads an assignment rvalue snapshot.
func assignment_f128(value as f128) -> i32 {
    new expected as f128 = 2.5
    new converted = to_i32(value = expected)
    if (converted != 2 || value != expected) { return 1; }
    expected = 3.0
    new measured = length(value = expected)
    if (measured != 16 || value != expected) { return 1; }
    return 0
}
func main() -> i32 {
    new failures as i32 = 0
    if (assignment_f16(1.0) != 0 || assignment_f32(1.0) != 0 || assignment_f64(1.0) != 0 || assignment_f128(1.0) != 0) { failures++; }
    if (global_length != 3 || first_call != 1 || second_length != 8 || init_order != 2) {
        failures++
    }
    if (self_length() != 8 || shadow_length() != 8 || mutual_first != 8 || mutual_second != 8) {
        failures++
    }
    new flag = true
    new predicate = 1 == 1
    if (length(flag) != 1 || length(predicate) != 1 || length(global_flag) != 1) {
        failures++
    }
    new left as f64 = 1.0
    new right as f64 = 2.0
    new bits = (flag ? left : right) as u64
    new nested = ((flag ? left : right) as bytes) as u64
    new indexed = bits[0] as u8
    new indirect = [8]*&bits
    new slice = bits[0:3]
    if (length(bits) != 8 || length(nested) != 8 || length(indexed) != 1 || length(indirect) != 8 || length(slice) != 3 || bits != nested) {
        failures++
    }
    new triple as bytes = resize_bytes(bits, 3)
    new wide as bytes = resize_bytes(bits, 16)
    new folded as bytes = resize_bytes(bits, 1 + 2)
    new swapped as bytes = byte_swap(triple)
    new derived as bytes = resize_bytes(bits, length(triple))
    if (length(triple) != 3 || length(wide) != 16 || length(folded) != 3 || length(swapped) != 3 || length(derived) != 3) {
        failures++
    }
    new uninitialized[17]
    if (length(uninitialized) != 17) { failures++; }
    calls = 0
    new measured = length(tick())
    if (measured != 8 || calls != 1) { failures++; }
    measured = length(float_tick())
    if (measured != 8 || calls != 2) { failures++; }
    measured = length(true ? tick() : tick())
    if (measured != 8 || calls != 3) { failures++; }
    measured = length((bits = tick()))
    if (measured != 8 || calls != 4 || bits != 0x0102030405060708) { failures++; }
    new inferred = tick()
    if (length(inferred) != 8 || calls != 5) { failures++; }
    measured = length(resize_bytes(tick(), width()))
    if (measured != 3 || calls != 7) { failures++; }
    measured = length(byte_swap(resize_bytes(tick(), width())))
    if (measured != 3 || calls != 9) { failures++; }
    measured = length(resize_bytes(tick(), width()) as bytes)
    if (measured != 3 || calls != 11) { failures++; }
    new requested as u64 = 8
    measured = length(resize_bytes(tick(), requested) as u64)
    if (measured != 8 || calls != 12) { failures++; }
    // A known length expression still has effects when used as a resize count.
    new eight as bytes = resize_bytes(bits, length(tick()))
    if (length(eight) != 8 || calls != 13) { failures++; }
    measured = length(once())
    measured = length(once())
    if (measured != 8 || calls != 14) { failures++; }
    new converted = to_i64(indexed = 7)
    new converted_float as f64 = to_f64(indexed = 9)
    if (converted != 7 || converted_float != 9.0 || indexed != 9) { failures++; }
    printf("view-lengths %d\n", failures)
    return failures
}
