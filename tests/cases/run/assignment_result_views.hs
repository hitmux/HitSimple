$include <string.hsh>
$include <stdlib.hsh>
$include <stdio.hsh>

// Standard 7.2: writes finish before an independent rvalue copy is formed.
new calls as i32 = 0
func mark_f16(value as f16) -> f16 { calls++; return value; }
func assign_f16(left as f16, right as f16) -> f16 { return left = right; }
func check_f16(value as f16) -> i32 {
    new failures as i32 = 0
    new target as f16
    new raw[2] as bytes
    new copied as addr = memcpy(raw, (target = value), 2)
    if (memcmp(raw, value, 2) != 0 || memcmp(target, value, 2) != 0) { failures++; }
    new typed as f16 = (target = value)
    if (memcmp(typed, value, 2) != 0) { failures++; }
    typed = assign_f16(target, value)
    if (memcmp(typed, value, 2) != 0) { failures++; }
    calls = 0
    copied = memcpy(raw, (target = mark_f16(value)), 2)
    if (calls != 1 || memcmp(raw, value, 2) != 0) { failures++; }
    new one as f16 = to_f16(1)
    new two as f16 = to_f16(2)
    // Each argument is its own snapshot, independent of argument order.
    if (memcmp((target = one), (target = two), 2) == 0) { failures++; }
    new index as i32 = 0
    while (index < 2048) {
        if (memcmp((target = value), value, 2) != 0) { failures++; }
        index++
    }
    return failures
}
func mark_f32(value as f32) -> f32 { calls++; return value; }
func assign_f32(left as f32, right as f32) -> f32 { return left = right; }
func check_f32(value as f32) -> i32 {
    new failures as i32 = 0
    new target as f32
    new raw[4] as bytes
    new copied as addr = memcpy(raw, (target = value), 4)
    if (memcmp(raw, value, 4) != 0 || memcmp(target, value, 4) != 0) { failures++; }
    new typed as f32 = (target = value)
    if (memcmp(typed, value, 4) != 0) { failures++; }
    typed = assign_f32(target, value)
    if (memcmp(typed, value, 4) != 0) { failures++; }
    calls = 0
    copied = memcpy(raw, (target = mark_f32(value)), 4)
    if (calls != 1 || memcmp(raw, value, 4) != 0) { failures++; }
    new one as f32 = to_f32(1)
    new two as f32 = to_f32(2)
    // Each argument is its own snapshot, independent of argument order.
    if (memcmp((target = one), (target = two), 4) == 0) { failures++; }
    new index as i32 = 0
    while (index < 2048) {
        if (memcmp((target = value), value, 4) != 0) { failures++; }
        index++
    }
    return failures
}
func mark_f64(value as f64) -> f64 { calls++; return value; }
func assign_f64(left as f64, right as f64) -> f64 { return left = right; }
func check_f64(value as f64) -> i32 {
    new failures as i32 = 0
    new target as f64
    new raw[8] as bytes
    new copied as addr = memcpy(raw, (target = value), 8)
    if (memcmp(raw, value, 8) != 0 || memcmp(target, value, 8) != 0) { failures++; }
    new typed as f64 = (target = value)
    if (memcmp(typed, value, 8) != 0) { failures++; }
    typed = assign_f64(target, value)
    if (memcmp(typed, value, 8) != 0) { failures++; }
    calls = 0
    copied = memcpy(raw, (target = mark_f64(value)), 8)
    if (calls != 1 || memcmp(raw, value, 8) != 0) { failures++; }
    new one as f64 = to_f64(1)
    new two as f64 = to_f64(2)
    // Each argument is its own snapshot, independent of argument order.
    if (memcmp((target = one), (target = two), 8) == 0) { failures++; }
    new index as i32 = 0
    while (index < 2048) {
        if (memcmp((target = value), value, 8) != 0) { failures++; }
        index++
    }
    return failures
}
func mark_f128(value as f128) -> f128 { calls++; return value; }
func assign_f128(left as f128, right as f128) -> f128 { return left = right; }
func check_f128(value as f128) -> i32 {
    new failures as i32 = 0
    new target as f128
    new raw[16] as bytes
    new copied as addr = memcpy(raw, (target = value), 16)
    if (memcmp(raw, value, 16) != 0 || memcmp(target, value, 16) != 0) { failures++; }
    new typed as f128 = (target = value)
    if (memcmp(typed, value, 16) != 0) { failures++; }
    typed = assign_f128(target, value)
    if (memcmp(typed, value, 16) != 0) { failures++; }
    calls = 0
    copied = memcpy(raw, (target = mark_f128(value)), 16)
    if (calls != 1 || memcmp(raw, value, 16) != 0) { failures++; }
    new one as f128 = to_f128(1)
    new two as f128 = to_f128(2)
    // Each argument is its own snapshot, independent of argument order.
    if (memcmp((target = one), (target = two), 16) == 0) { failures++; }
    new index as i32 = 0
    while (index < 2048) {
        if (memcmp((target = value), value, 16) != 0) { failures++; }
        index++
    }
    return failures
}

template Counter { value[4] as i32; }
impl Counter {
    op = (dst as Counter, src as Counter) -> Counter {
        dst.value = src.value + 1
        return src
    }
}
func main() -> i32 {
    new failures as i32 = 0
    new bits16 as u16 = 0x7e71
    new bits32 as u32 = 0x7fc00071
    new bits64 as u64 = 0x7ff8000000000071
    failures = failures + check_f16(bits16 as f16)
    failures = failures + check_f32(bits32 as f32)
    failures = failures + check_f64(bits64 as f64)
    bits16 = 0x8000
    bits32 = 0x80000000
    bits64 = 0x8000000000000000
    failures = failures + check_f16(bits16 as f16)
    failures = failures + check_f32(bits32 as f32)
    failures = failures + check_f64(bits64 as f64)
    // Supported little-endian binary128 representations.
    new bits128[16] as bytes
    memset(bits128, 0, 16)
    bits128[0] = 0x71
    bits128[13] = 0x80
    bits128[14] = 0xff
    bits128[15] = 0x7f
    failures = failures + check_f128(bits128 as f128)
    memset(bits128, 0, 16)
    bits128[15] = 0x80
    failures = failures + check_f128(bits128 as f128)
    new byte as u8 = 0
    if (memcmp((byte = 1), (byte = 2), 1) == 0) { failures++; }
    new source as Counter
    new target as Counter
    source.value = 7
    new raw[4] as bytes
    new copied as addr = memcpy(raw, (target = source), 4)
    new expected as i32 = 8
    if (memcmp(raw, expected, 4) != 0 || target.value != 8) { failures++; }
    new text[8] as cstr
    new output[8] as bytes
    copied = memcpy(output, (text %s= "ab"), 8)
    if (memcmp(output, text, 8) != 0) { failures++; }
    printf("assignment-views %d\n", failures)
    if (failures != 0) { return 1; }
    return 0
}
