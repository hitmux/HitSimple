$include <stdlib.hsh>
$include <string.hsh>
$include <stdio.hsh>

// Standard 7.8: the oracle copies address-order prefixes and zero pads.
// memcpy/memset and byte comparison are independent of resize lowering.
new calls as i32 = 0
new shared as f64 = 1.5
template Packet { data[5] as bytes; }

func identity_f16(value as f16) -> f16 { calls++; return value; }
func check_f16(value as f16) -> i32 {
    new failures as i32 = 0
    new expected1[1] as bytes
    memset(expected1, 0, 1)
    _ = memcpy(expected1, value, 1)
    new actual1[1] as bytes = resize_bytes(value, 1) as bytes
    if (memcmp(actual1, expected1, 1) != 0) { failures++; }
    calls = 0
    actual1 = resize_bytes(identity_f16(value), 1) as bytes
    if (calls != 1 || memcmp(actual1, expected1, 1) != 0) { failures++; }
    calls = 0
    actual1 = resize_bytes(true ? identity_f16(value) : value, 1) as bytes
    if (calls != 1 || memcmp(actual1, expected1, 1) != 0) { failures++; }
    new expected2[2] as bytes
    memset(expected2, 0, 2)
    _ = memcpy(expected2, value, 2)
    new actual2[2] as bytes = resize_bytes(value, 2) as bytes
    if (memcmp(actual2, expected2, 2) != 0) { failures++; }
    calls = 0
    actual2 = resize_bytes(identity_f16(value), 2) as bytes
    if (calls != 1 || memcmp(actual2, expected2, 2) != 0) { failures++; }
    calls = 0
    actual2 = resize_bytes(true ? identity_f16(value) : value, 2) as bytes
    if (calls != 1 || memcmp(actual2, expected2, 2) != 0) { failures++; }
    new expected4[4] as bytes
    memset(expected4, 0, 4)
    _ = memcpy(expected4, value, 2)
    new actual4[4] as bytes = resize_bytes(value, 4) as bytes
    if (memcmp(actual4, expected4, 4) != 0) { failures++; }
    calls = 0
    actual4 = resize_bytes(identity_f16(value), 4) as bytes
    if (calls != 1 || memcmp(actual4, expected4, 4) != 0) { failures++; }
    calls = 0
    actual4 = resize_bytes(true ? identity_f16(value) : value, 4) as bytes
    if (calls != 1 || memcmp(actual4, expected4, 4) != 0) { failures++; }
    new expected8[8] as bytes
    memset(expected8, 0, 8)
    _ = memcpy(expected8, value, 2)
    new actual8[8] as bytes = resize_bytes(value, 8) as bytes
    if (memcmp(actual8, expected8, 8) != 0) { failures++; }
    calls = 0
    actual8 = resize_bytes(identity_f16(value), 8) as bytes
    if (calls != 1 || memcmp(actual8, expected8, 8) != 0) { failures++; }
    calls = 0
    actual8 = resize_bytes(true ? identity_f16(value) : value, 8) as bytes
    if (calls != 1 || memcmp(actual8, expected8, 8) != 0) { failures++; }
    new expected3[3] as bytes
    memset(expected3, 0, 3)
    _ = memcpy(expected3, value, 2)
    new actual3[3] as bytes = resize_bytes(value, 3) as bytes
    if (memcmp(actual3, expected3, 3) != 0) { failures++; }
    calls = 0
    actual3 = resize_bytes(identity_f16(value), 3) as bytes
    if (calls != 1 || memcmp(actual3, expected3, 3) != 0) { failures++; }
    calls = 0
    actual3 = resize_bytes(true ? identity_f16(value) : value, 3) as bytes
    if (calls != 1 || memcmp(actual3, expected3, 3) != 0) { failures++; }
    new expected16[16] as bytes
    memset(expected16, 0, 16)
    _ = memcpy(expected16, value, 2)
    new actual16[16] as bytes = resize_bytes(value, 16) as bytes
    if (memcmp(actual16, expected16, 16) != 0) { failures++; }
    calls = 0
    actual16 = resize_bytes(identity_f16(value), 16) as bytes
    if (calls != 1 || memcmp(actual16, expected16, 16) != 0) { failures++; }
    calls = 0
    actual16 = resize_bytes(true ? identity_f16(value) : value, 16) as bytes
    if (calls != 1 || memcmp(actual16, expected16, 16) != 0) { failures++; }
    return failures
}

func identity_f32(value as f32) -> f32 { calls++; return value; }
func check_f32(value as f32) -> i32 {
    new failures as i32 = 0
    new expected1[1] as bytes
    memset(expected1, 0, 1)
    _ = memcpy(expected1, value, 1)
    new actual1[1] as bytes = resize_bytes(value, 1) as bytes
    if (memcmp(actual1, expected1, 1) != 0) { failures++; }
    calls = 0
    actual1 = resize_bytes(identity_f32(value), 1) as bytes
    if (calls != 1 || memcmp(actual1, expected1, 1) != 0) { failures++; }
    calls = 0
    actual1 = resize_bytes(true ? identity_f32(value) : value, 1) as bytes
    if (calls != 1 || memcmp(actual1, expected1, 1) != 0) { failures++; }
    new expected2[2] as bytes
    memset(expected2, 0, 2)
    _ = memcpy(expected2, value, 2)
    new actual2[2] as bytes = resize_bytes(value, 2) as bytes
    if (memcmp(actual2, expected2, 2) != 0) { failures++; }
    calls = 0
    actual2 = resize_bytes(identity_f32(value), 2) as bytes
    if (calls != 1 || memcmp(actual2, expected2, 2) != 0) { failures++; }
    calls = 0
    actual2 = resize_bytes(true ? identity_f32(value) : value, 2) as bytes
    if (calls != 1 || memcmp(actual2, expected2, 2) != 0) { failures++; }
    new expected4[4] as bytes
    memset(expected4, 0, 4)
    _ = memcpy(expected4, value, 4)
    new actual4[4] as bytes = resize_bytes(value, 4) as bytes
    if (memcmp(actual4, expected4, 4) != 0) { failures++; }
    calls = 0
    actual4 = resize_bytes(identity_f32(value), 4) as bytes
    if (calls != 1 || memcmp(actual4, expected4, 4) != 0) { failures++; }
    calls = 0
    actual4 = resize_bytes(true ? identity_f32(value) : value, 4) as bytes
    if (calls != 1 || memcmp(actual4, expected4, 4) != 0) { failures++; }
    new expected8[8] as bytes
    memset(expected8, 0, 8)
    _ = memcpy(expected8, value, 4)
    new actual8[8] as bytes = resize_bytes(value, 8) as bytes
    if (memcmp(actual8, expected8, 8) != 0) { failures++; }
    calls = 0
    actual8 = resize_bytes(identity_f32(value), 8) as bytes
    if (calls != 1 || memcmp(actual8, expected8, 8) != 0) { failures++; }
    calls = 0
    actual8 = resize_bytes(true ? identity_f32(value) : value, 8) as bytes
    if (calls != 1 || memcmp(actual8, expected8, 8) != 0) { failures++; }
    new expected3[3] as bytes
    memset(expected3, 0, 3)
    _ = memcpy(expected3, value, 3)
    new actual3[3] as bytes = resize_bytes(value, 3) as bytes
    if (memcmp(actual3, expected3, 3) != 0) { failures++; }
    calls = 0
    actual3 = resize_bytes(identity_f32(value), 3) as bytes
    if (calls != 1 || memcmp(actual3, expected3, 3) != 0) { failures++; }
    calls = 0
    actual3 = resize_bytes(true ? identity_f32(value) : value, 3) as bytes
    if (calls != 1 || memcmp(actual3, expected3, 3) != 0) { failures++; }
    new expected16[16] as bytes
    memset(expected16, 0, 16)
    _ = memcpy(expected16, value, 4)
    new actual16[16] as bytes = resize_bytes(value, 16) as bytes
    if (memcmp(actual16, expected16, 16) != 0) { failures++; }
    calls = 0
    actual16 = resize_bytes(identity_f32(value), 16) as bytes
    if (calls != 1 || memcmp(actual16, expected16, 16) != 0) { failures++; }
    calls = 0
    actual16 = resize_bytes(true ? identity_f32(value) : value, 16) as bytes
    if (calls != 1 || memcmp(actual16, expected16, 16) != 0) { failures++; }
    return failures
}

func identity_f64(value as f64) -> f64 { calls++; return value; }
func check_f64(value as f64) -> i32 {
    new failures as i32 = 0
    new expected1[1] as bytes
    memset(expected1, 0, 1)
    _ = memcpy(expected1, value, 1)
    new actual1[1] as bytes = resize_bytes(value, 1) as bytes
    if (memcmp(actual1, expected1, 1) != 0) { failures++; }
    calls = 0
    actual1 = resize_bytes(identity_f64(value), 1) as bytes
    if (calls != 1 || memcmp(actual1, expected1, 1) != 0) { failures++; }
    calls = 0
    actual1 = resize_bytes(true ? identity_f64(value) : value, 1) as bytes
    if (calls != 1 || memcmp(actual1, expected1, 1) != 0) { failures++; }
    new expected2[2] as bytes
    memset(expected2, 0, 2)
    _ = memcpy(expected2, value, 2)
    new actual2[2] as bytes = resize_bytes(value, 2) as bytes
    if (memcmp(actual2, expected2, 2) != 0) { failures++; }
    calls = 0
    actual2 = resize_bytes(identity_f64(value), 2) as bytes
    if (calls != 1 || memcmp(actual2, expected2, 2) != 0) { failures++; }
    calls = 0
    actual2 = resize_bytes(true ? identity_f64(value) : value, 2) as bytes
    if (calls != 1 || memcmp(actual2, expected2, 2) != 0) { failures++; }
    new expected4[4] as bytes
    memset(expected4, 0, 4)
    _ = memcpy(expected4, value, 4)
    new actual4[4] as bytes = resize_bytes(value, 4) as bytes
    if (memcmp(actual4, expected4, 4) != 0) { failures++; }
    calls = 0
    actual4 = resize_bytes(identity_f64(value), 4) as bytes
    if (calls != 1 || memcmp(actual4, expected4, 4) != 0) { failures++; }
    calls = 0
    actual4 = resize_bytes(true ? identity_f64(value) : value, 4) as bytes
    if (calls != 1 || memcmp(actual4, expected4, 4) != 0) { failures++; }
    new expected8[8] as bytes
    memset(expected8, 0, 8)
    _ = memcpy(expected8, value, 8)
    new actual8[8] as bytes = resize_bytes(value, 8) as bytes
    if (memcmp(actual8, expected8, 8) != 0) { failures++; }
    calls = 0
    actual8 = resize_bytes(identity_f64(value), 8) as bytes
    if (calls != 1 || memcmp(actual8, expected8, 8) != 0) { failures++; }
    calls = 0
    actual8 = resize_bytes(true ? identity_f64(value) : value, 8) as bytes
    if (calls != 1 || memcmp(actual8, expected8, 8) != 0) { failures++; }
    new expected3[3] as bytes
    memset(expected3, 0, 3)
    _ = memcpy(expected3, value, 3)
    new actual3[3] as bytes = resize_bytes(value, 3) as bytes
    if (memcmp(actual3, expected3, 3) != 0) { failures++; }
    calls = 0
    actual3 = resize_bytes(identity_f64(value), 3) as bytes
    if (calls != 1 || memcmp(actual3, expected3, 3) != 0) { failures++; }
    calls = 0
    actual3 = resize_bytes(true ? identity_f64(value) : value, 3) as bytes
    if (calls != 1 || memcmp(actual3, expected3, 3) != 0) { failures++; }
    new expected16[16] as bytes
    memset(expected16, 0, 16)
    _ = memcpy(expected16, value, 8)
    new actual16[16] as bytes = resize_bytes(value, 16) as bytes
    if (memcmp(actual16, expected16, 16) != 0) { failures++; }
    calls = 0
    actual16 = resize_bytes(identity_f64(value), 16) as bytes
    if (calls != 1 || memcmp(actual16, expected16, 16) != 0) { failures++; }
    calls = 0
    actual16 = resize_bytes(true ? identity_f64(value) : value, 16) as bytes
    if (calls != 1 || memcmp(actual16, expected16, 16) != 0) { failures++; }
    return failures
}

func identity_f128(value as f128) -> f128 { calls++; return value; }
func check_f128(value as f128) -> i32 {
    new failures as i32 = 0
    new expected1[1] as bytes
    memset(expected1, 0, 1)
    _ = memcpy(expected1, value, 1)
    new actual1[1] as bytes = resize_bytes(value, 1) as bytes
    if (memcmp(actual1, expected1, 1) != 0) { failures++; }
    calls = 0
    actual1 = resize_bytes(identity_f128(value), 1) as bytes
    if (calls != 1 || memcmp(actual1, expected1, 1) != 0) { failures++; }
    calls = 0
    actual1 = resize_bytes(true ? identity_f128(value) : value, 1) as bytes
    if (calls != 1 || memcmp(actual1, expected1, 1) != 0) { failures++; }
    new expected2[2] as bytes
    memset(expected2, 0, 2)
    _ = memcpy(expected2, value, 2)
    new actual2[2] as bytes = resize_bytes(value, 2) as bytes
    if (memcmp(actual2, expected2, 2) != 0) { failures++; }
    calls = 0
    actual2 = resize_bytes(identity_f128(value), 2) as bytes
    if (calls != 1 || memcmp(actual2, expected2, 2) != 0) { failures++; }
    calls = 0
    actual2 = resize_bytes(true ? identity_f128(value) : value, 2) as bytes
    if (calls != 1 || memcmp(actual2, expected2, 2) != 0) { failures++; }
    new expected4[4] as bytes
    memset(expected4, 0, 4)
    _ = memcpy(expected4, value, 4)
    new actual4[4] as bytes = resize_bytes(value, 4) as bytes
    if (memcmp(actual4, expected4, 4) != 0) { failures++; }
    calls = 0
    actual4 = resize_bytes(identity_f128(value), 4) as bytes
    if (calls != 1 || memcmp(actual4, expected4, 4) != 0) { failures++; }
    calls = 0
    actual4 = resize_bytes(true ? identity_f128(value) : value, 4) as bytes
    if (calls != 1 || memcmp(actual4, expected4, 4) != 0) { failures++; }
    new expected8[8] as bytes
    memset(expected8, 0, 8)
    _ = memcpy(expected8, value, 8)
    new actual8[8] as bytes = resize_bytes(value, 8) as bytes
    if (memcmp(actual8, expected8, 8) != 0) { failures++; }
    calls = 0
    actual8 = resize_bytes(identity_f128(value), 8) as bytes
    if (calls != 1 || memcmp(actual8, expected8, 8) != 0) { failures++; }
    calls = 0
    actual8 = resize_bytes(true ? identity_f128(value) : value, 8) as bytes
    if (calls != 1 || memcmp(actual8, expected8, 8) != 0) { failures++; }
    new expected3[3] as bytes
    memset(expected3, 0, 3)
    _ = memcpy(expected3, value, 3)
    new actual3[3] as bytes = resize_bytes(value, 3) as bytes
    if (memcmp(actual3, expected3, 3) != 0) { failures++; }
    calls = 0
    actual3 = resize_bytes(identity_f128(value), 3) as bytes
    if (calls != 1 || memcmp(actual3, expected3, 3) != 0) { failures++; }
    calls = 0
    actual3 = resize_bytes(true ? identity_f128(value) : value, 3) as bytes
    if (calls != 1 || memcmp(actual3, expected3, 3) != 0) { failures++; }
    new expected16[16] as bytes
    memset(expected16, 0, 16)
    _ = memcpy(expected16, value, 16)
    new actual16[16] as bytes = resize_bytes(value, 16) as bytes
    if (memcmp(actual16, expected16, 16) != 0) { failures++; }
    calls = 0
    actual16 = resize_bytes(identity_f128(value), 16) as bytes
    if (calls != 1 || memcmp(actual16, expected16, 16) != 0) { failures++; }
    calls = 0
    actual16 = resize_bytes(true ? identity_f128(value) : value, 16) as bytes
    if (calls != 1 || memcmp(actual16, expected16, 16) != 0) { failures++; }
    return failures
}

func identity_i8(value as i8) -> i8 { calls++; return value; }
func check_i8(value as i8) -> i32 {
    new failures as i32 = 0
    new expected1[1] as bytes
    memset(expected1, 0, 1)
    _ = memcpy(expected1, value, 1)
    new actual1[1] as bytes = resize_bytes(value, 1) as bytes
    if (memcmp(actual1, expected1, 1) != 0) { failures++; }
    calls = 0
    actual1 = resize_bytes(identity_i8(value), 1) as bytes
    if (calls != 1 || memcmp(actual1, expected1, 1) != 0) { failures++; }
    new expected2[2] as bytes
    memset(expected2, 0, 2)
    _ = memcpy(expected2, value, 1)
    new actual2[2] as bytes = resize_bytes(value, 2) as bytes
    if (memcmp(actual2, expected2, 2) != 0) { failures++; }
    calls = 0
    actual2 = resize_bytes(identity_i8(value), 2) as bytes
    if (calls != 1 || memcmp(actual2, expected2, 2) != 0) { failures++; }
    new expected4[4] as bytes
    memset(expected4, 0, 4)
    _ = memcpy(expected4, value, 1)
    new actual4[4] as bytes = resize_bytes(value, 4) as bytes
    if (memcmp(actual4, expected4, 4) != 0) { failures++; }
    calls = 0
    actual4 = resize_bytes(identity_i8(value), 4) as bytes
    if (calls != 1 || memcmp(actual4, expected4, 4) != 0) { failures++; }
    new expected8[8] as bytes
    memset(expected8, 0, 8)
    _ = memcpy(expected8, value, 1)
    new actual8[8] as bytes = resize_bytes(value, 8) as bytes
    if (memcmp(actual8, expected8, 8) != 0) { failures++; }
    calls = 0
    actual8 = resize_bytes(identity_i8(value), 8) as bytes
    if (calls != 1 || memcmp(actual8, expected8, 8) != 0) { failures++; }
    return failures
}

func identity_i16(value as i16) -> i16 { calls++; return value; }
func check_i16(value as i16) -> i32 {
    new failures as i32 = 0
    new expected1[1] as bytes
    memset(expected1, 0, 1)
    _ = memcpy(expected1, value, 1)
    new actual1[1] as bytes = resize_bytes(value, 1) as bytes
    if (memcmp(actual1, expected1, 1) != 0) { failures++; }
    calls = 0
    actual1 = resize_bytes(identity_i16(value), 1) as bytes
    if (calls != 1 || memcmp(actual1, expected1, 1) != 0) { failures++; }
    new expected2[2] as bytes
    memset(expected2, 0, 2)
    _ = memcpy(expected2, value, 2)
    new actual2[2] as bytes = resize_bytes(value, 2) as bytes
    if (memcmp(actual2, expected2, 2) != 0) { failures++; }
    calls = 0
    actual2 = resize_bytes(identity_i16(value), 2) as bytes
    if (calls != 1 || memcmp(actual2, expected2, 2) != 0) { failures++; }
    new expected4[4] as bytes
    memset(expected4, 0, 4)
    _ = memcpy(expected4, value, 2)
    new actual4[4] as bytes = resize_bytes(value, 4) as bytes
    if (memcmp(actual4, expected4, 4) != 0) { failures++; }
    calls = 0
    actual4 = resize_bytes(identity_i16(value), 4) as bytes
    if (calls != 1 || memcmp(actual4, expected4, 4) != 0) { failures++; }
    new expected8[8] as bytes
    memset(expected8, 0, 8)
    _ = memcpy(expected8, value, 2)
    new actual8[8] as bytes = resize_bytes(value, 8) as bytes
    if (memcmp(actual8, expected8, 8) != 0) { failures++; }
    calls = 0
    actual8 = resize_bytes(identity_i16(value), 8) as bytes
    if (calls != 1 || memcmp(actual8, expected8, 8) != 0) { failures++; }
    return failures
}

func identity_i32(value as i32) -> i32 { calls++; return value; }
func check_i32(value as i32) -> i32 {
    new failures as i32 = 0
    new expected1[1] as bytes
    memset(expected1, 0, 1)
    _ = memcpy(expected1, value, 1)
    new actual1[1] as bytes = resize_bytes(value, 1) as bytes
    if (memcmp(actual1, expected1, 1) != 0) { failures++; }
    calls = 0
    actual1 = resize_bytes(identity_i32(value), 1) as bytes
    if (calls != 1 || memcmp(actual1, expected1, 1) != 0) { failures++; }
    new expected2[2] as bytes
    memset(expected2, 0, 2)
    _ = memcpy(expected2, value, 2)
    new actual2[2] as bytes = resize_bytes(value, 2) as bytes
    if (memcmp(actual2, expected2, 2) != 0) { failures++; }
    calls = 0
    actual2 = resize_bytes(identity_i32(value), 2) as bytes
    if (calls != 1 || memcmp(actual2, expected2, 2) != 0) { failures++; }
    new expected4[4] as bytes
    memset(expected4, 0, 4)
    _ = memcpy(expected4, value, 4)
    new actual4[4] as bytes = resize_bytes(value, 4) as bytes
    if (memcmp(actual4, expected4, 4) != 0) { failures++; }
    calls = 0
    actual4 = resize_bytes(identity_i32(value), 4) as bytes
    if (calls != 1 || memcmp(actual4, expected4, 4) != 0) { failures++; }
    new expected8[8] as bytes
    memset(expected8, 0, 8)
    _ = memcpy(expected8, value, 4)
    new actual8[8] as bytes = resize_bytes(value, 8) as bytes
    if (memcmp(actual8, expected8, 8) != 0) { failures++; }
    calls = 0
    actual8 = resize_bytes(identity_i32(value), 8) as bytes
    if (calls != 1 || memcmp(actual8, expected8, 8) != 0) { failures++; }
    return failures
}

func identity_i64(value as i64) -> i64 { calls++; return value; }
func check_i64(value as i64) -> i32 {
    new failures as i32 = 0
    new expected1[1] as bytes
    memset(expected1, 0, 1)
    _ = memcpy(expected1, value, 1)
    new actual1[1] as bytes = resize_bytes(value, 1) as bytes
    if (memcmp(actual1, expected1, 1) != 0) { failures++; }
    calls = 0
    actual1 = resize_bytes(identity_i64(value), 1) as bytes
    if (calls != 1 || memcmp(actual1, expected1, 1) != 0) { failures++; }
    new expected2[2] as bytes
    memset(expected2, 0, 2)
    _ = memcpy(expected2, value, 2)
    new actual2[2] as bytes = resize_bytes(value, 2) as bytes
    if (memcmp(actual2, expected2, 2) != 0) { failures++; }
    calls = 0
    actual2 = resize_bytes(identity_i64(value), 2) as bytes
    if (calls != 1 || memcmp(actual2, expected2, 2) != 0) { failures++; }
    new expected4[4] as bytes
    memset(expected4, 0, 4)
    _ = memcpy(expected4, value, 4)
    new actual4[4] as bytes = resize_bytes(value, 4) as bytes
    if (memcmp(actual4, expected4, 4) != 0) { failures++; }
    calls = 0
    actual4 = resize_bytes(identity_i64(value), 4) as bytes
    if (calls != 1 || memcmp(actual4, expected4, 4) != 0) { failures++; }
    new expected8[8] as bytes
    memset(expected8, 0, 8)
    _ = memcpy(expected8, value, 8)
    new actual8[8] as bytes = resize_bytes(value, 8) as bytes
    if (memcmp(actual8, expected8, 8) != 0) { failures++; }
    calls = 0
    actual8 = resize_bytes(identity_i64(value), 8) as bytes
    if (calls != 1 || memcmp(actual8, expected8, 8) != 0) { failures++; }
    return failures
}

func identity_u8(value as u8) -> u8 { calls++; return value; }
func check_u8(value as u8) -> i32 {
    new failures as i32 = 0
    new expected1[1] as bytes
    memset(expected1, 0, 1)
    _ = memcpy(expected1, value, 1)
    new actual1[1] as bytes = resize_bytes(value, 1) as bytes
    if (memcmp(actual1, expected1, 1) != 0) { failures++; }
    calls = 0
    actual1 = resize_bytes(identity_u8(value), 1) as bytes
    if (calls != 1 || memcmp(actual1, expected1, 1) != 0) { failures++; }
    new expected2[2] as bytes
    memset(expected2, 0, 2)
    _ = memcpy(expected2, value, 1)
    new actual2[2] as bytes = resize_bytes(value, 2) as bytes
    if (memcmp(actual2, expected2, 2) != 0) { failures++; }
    calls = 0
    actual2 = resize_bytes(identity_u8(value), 2) as bytes
    if (calls != 1 || memcmp(actual2, expected2, 2) != 0) { failures++; }
    new expected4[4] as bytes
    memset(expected4, 0, 4)
    _ = memcpy(expected4, value, 1)
    new actual4[4] as bytes = resize_bytes(value, 4) as bytes
    if (memcmp(actual4, expected4, 4) != 0) { failures++; }
    calls = 0
    actual4 = resize_bytes(identity_u8(value), 4) as bytes
    if (calls != 1 || memcmp(actual4, expected4, 4) != 0) { failures++; }
    new expected8[8] as bytes
    memset(expected8, 0, 8)
    _ = memcpy(expected8, value, 1)
    new actual8[8] as bytes = resize_bytes(value, 8) as bytes
    if (memcmp(actual8, expected8, 8) != 0) { failures++; }
    calls = 0
    actual8 = resize_bytes(identity_u8(value), 8) as bytes
    if (calls != 1 || memcmp(actual8, expected8, 8) != 0) { failures++; }
    return failures
}

func identity_u16(value as u16) -> u16 { calls++; return value; }
func check_u16(value as u16) -> i32 {
    new failures as i32 = 0
    new expected1[1] as bytes
    memset(expected1, 0, 1)
    _ = memcpy(expected1, value, 1)
    new actual1[1] as bytes = resize_bytes(value, 1) as bytes
    if (memcmp(actual1, expected1, 1) != 0) { failures++; }
    calls = 0
    actual1 = resize_bytes(identity_u16(value), 1) as bytes
    if (calls != 1 || memcmp(actual1, expected1, 1) != 0) { failures++; }
    new expected2[2] as bytes
    memset(expected2, 0, 2)
    _ = memcpy(expected2, value, 2)
    new actual2[2] as bytes = resize_bytes(value, 2) as bytes
    if (memcmp(actual2, expected2, 2) != 0) { failures++; }
    calls = 0
    actual2 = resize_bytes(identity_u16(value), 2) as bytes
    if (calls != 1 || memcmp(actual2, expected2, 2) != 0) { failures++; }
    new expected4[4] as bytes
    memset(expected4, 0, 4)
    _ = memcpy(expected4, value, 2)
    new actual4[4] as bytes = resize_bytes(value, 4) as bytes
    if (memcmp(actual4, expected4, 4) != 0) { failures++; }
    calls = 0
    actual4 = resize_bytes(identity_u16(value), 4) as bytes
    if (calls != 1 || memcmp(actual4, expected4, 4) != 0) { failures++; }
    new expected8[8] as bytes
    memset(expected8, 0, 8)
    _ = memcpy(expected8, value, 2)
    new actual8[8] as bytes = resize_bytes(value, 8) as bytes
    if (memcmp(actual8, expected8, 8) != 0) { failures++; }
    calls = 0
    actual8 = resize_bytes(identity_u16(value), 8) as bytes
    if (calls != 1 || memcmp(actual8, expected8, 8) != 0) { failures++; }
    return failures
}

func identity_u32(value as u32) -> u32 { calls++; return value; }
func check_u32(value as u32) -> i32 {
    new failures as i32 = 0
    new expected1[1] as bytes
    memset(expected1, 0, 1)
    _ = memcpy(expected1, value, 1)
    new actual1[1] as bytes = resize_bytes(value, 1) as bytes
    if (memcmp(actual1, expected1, 1) != 0) { failures++; }
    calls = 0
    actual1 = resize_bytes(identity_u32(value), 1) as bytes
    if (calls != 1 || memcmp(actual1, expected1, 1) != 0) { failures++; }
    new expected2[2] as bytes
    memset(expected2, 0, 2)
    _ = memcpy(expected2, value, 2)
    new actual2[2] as bytes = resize_bytes(value, 2) as bytes
    if (memcmp(actual2, expected2, 2) != 0) { failures++; }
    calls = 0
    actual2 = resize_bytes(identity_u32(value), 2) as bytes
    if (calls != 1 || memcmp(actual2, expected2, 2) != 0) { failures++; }
    new expected4[4] as bytes
    memset(expected4, 0, 4)
    _ = memcpy(expected4, value, 4)
    new actual4[4] as bytes = resize_bytes(value, 4) as bytes
    if (memcmp(actual4, expected4, 4) != 0) { failures++; }
    calls = 0
    actual4 = resize_bytes(identity_u32(value), 4) as bytes
    if (calls != 1 || memcmp(actual4, expected4, 4) != 0) { failures++; }
    new expected8[8] as bytes
    memset(expected8, 0, 8)
    _ = memcpy(expected8, value, 4)
    new actual8[8] as bytes = resize_bytes(value, 8) as bytes
    if (memcmp(actual8, expected8, 8) != 0) { failures++; }
    calls = 0
    actual8 = resize_bytes(identity_u32(value), 8) as bytes
    if (calls != 1 || memcmp(actual8, expected8, 8) != 0) { failures++; }
    return failures
}

func identity_u64(value as u64) -> u64 { calls++; return value; }
func check_u64(value as u64) -> i32 {
    new failures as i32 = 0
    new expected1[1] as bytes
    memset(expected1, 0, 1)
    _ = memcpy(expected1, value, 1)
    new actual1[1] as bytes = resize_bytes(value, 1) as bytes
    if (memcmp(actual1, expected1, 1) != 0) { failures++; }
    calls = 0
    actual1 = resize_bytes(identity_u64(value), 1) as bytes
    if (calls != 1 || memcmp(actual1, expected1, 1) != 0) { failures++; }
    new expected2[2] as bytes
    memset(expected2, 0, 2)
    _ = memcpy(expected2, value, 2)
    new actual2[2] as bytes = resize_bytes(value, 2) as bytes
    if (memcmp(actual2, expected2, 2) != 0) { failures++; }
    calls = 0
    actual2 = resize_bytes(identity_u64(value), 2) as bytes
    if (calls != 1 || memcmp(actual2, expected2, 2) != 0) { failures++; }
    new expected4[4] as bytes
    memset(expected4, 0, 4)
    _ = memcpy(expected4, value, 4)
    new actual4[4] as bytes = resize_bytes(value, 4) as bytes
    if (memcmp(actual4, expected4, 4) != 0) { failures++; }
    calls = 0
    actual4 = resize_bytes(identity_u64(value), 4) as bytes
    if (calls != 1 || memcmp(actual4, expected4, 4) != 0) { failures++; }
    new expected8[8] as bytes
    memset(expected8, 0, 8)
    _ = memcpy(expected8, value, 8)
    new actual8[8] as bytes = resize_bytes(value, 8) as bytes
    if (memcmp(actual8, expected8, 8) != 0) { failures++; }
    calls = 0
    actual8 = resize_bytes(identity_u64(value), 8) as bytes
    if (calls != 1 || memcmp(actual8, expected8, 8) != 0) { failures++; }
    return failures
}

func identity_bool(value as bool) -> bool { calls++; return value; }
func check_bool(value as bool) -> i32 {
    new failures as i32 = 0
    new expected1[1] as bytes
    memset(expected1, 0, 1)
    _ = memcpy(expected1, value, 1)
    new actual1[1] as bytes = resize_bytes(value, 1) as bytes
    if (memcmp(actual1, expected1, 1) != 0) { failures++; }
    calls = 0
    actual1 = resize_bytes(identity_bool(value), 1) as bytes
    if (calls != 1 || memcmp(actual1, expected1, 1) != 0) { failures++; }
    new expected2[2] as bytes
    memset(expected2, 0, 2)
    _ = memcpy(expected2, value, 1)
    new actual2[2] as bytes = resize_bytes(value, 2) as bytes
    if (memcmp(actual2, expected2, 2) != 0) { failures++; }
    calls = 0
    actual2 = resize_bytes(identity_bool(value), 2) as bytes
    if (calls != 1 || memcmp(actual2, expected2, 2) != 0) { failures++; }
    new expected4[4] as bytes
    memset(expected4, 0, 4)
    _ = memcpy(expected4, value, 1)
    new actual4[4] as bytes = resize_bytes(value, 4) as bytes
    if (memcmp(actual4, expected4, 4) != 0) { failures++; }
    calls = 0
    actual4 = resize_bytes(identity_bool(value), 4) as bytes
    if (calls != 1 || memcmp(actual4, expected4, 4) != 0) { failures++; }
    new expected8[8] as bytes
    memset(expected8, 0, 8)
    _ = memcpy(expected8, value, 1)
    new actual8[8] as bytes = resize_bytes(value, 8) as bytes
    if (memcmp(actual8, expected8, 8) != 0) { failures++; }
    calls = 0
    actual8 = resize_bytes(identity_bool(value), 8) as bytes
    if (calls != 1 || memcmp(actual8, expected8, 8) != 0) { failures++; }
    return failures
}

func identity_addr(value as addr) -> addr { calls++; return value; }
func check_addr(value as addr) -> i32 {
    new failures as i32 = 0
    new expected1[1] as bytes
    memset(expected1, 0, 1)
    _ = memcpy(expected1, value as bytes, 1)
    new actual1[1] as bytes = resize_bytes(value, 1) as bytes
    if (memcmp(actual1, expected1, 1) != 0) { failures++; }
    calls = 0
    actual1 = resize_bytes(identity_addr(value), 1) as bytes
    if (calls != 1 || memcmp(actual1, expected1, 1) != 0) { failures++; }
    new expected2[2] as bytes
    memset(expected2, 0, 2)
    _ = memcpy(expected2, value as bytes, 2)
    new actual2[2] as bytes = resize_bytes(value, 2) as bytes
    if (memcmp(actual2, expected2, 2) != 0) { failures++; }
    calls = 0
    actual2 = resize_bytes(identity_addr(value), 2) as bytes
    if (calls != 1 || memcmp(actual2, expected2, 2) != 0) { failures++; }
    new expected4[4] as bytes
    memset(expected4, 0, 4)
    _ = memcpy(expected4, value as bytes, 4)
    new actual4[4] as bytes = resize_bytes(value, 4) as bytes
    if (memcmp(actual4, expected4, 4) != 0) { failures++; }
    calls = 0
    actual4 = resize_bytes(identity_addr(value), 4) as bytes
    if (calls != 1 || memcmp(actual4, expected4, 4) != 0) { failures++; }
    new expected8[8] as bytes
    memset(expected8, 0, 8)
    _ = memcpy(expected8, value as bytes, 8)
    new actual8[8] as bytes = resize_bytes(value, 8) as bytes
    if (memcmp(actual8, expected8, 8) != 0) { failures++; }
    calls = 0
    actual8 = resize_bytes(identity_addr(value), 8) as bytes
    if (calls != 1 || memcmp(actual8, expected8, 8) != 0) { failures++; }
    return failures
}

func identity_bytes(value[3] as bytes) -> [3] as bytes { calls++; return value; }
func check_bytes(value[3] as bytes) -> i32 {
    new failures as i32 = 0
    new expected1[1] as bytes
    memset(expected1, 0, 1)
    _ = memcpy(expected1, value, 1)
    new actual1[1] as bytes = resize_bytes(value, 1) as bytes
    if (memcmp(actual1, expected1, 1) != 0) { failures++; }
    calls = 0
    actual1 = resize_bytes(identity_bytes(value), 1) as bytes
    if (calls != 1 || memcmp(actual1, expected1, 1) != 0) { failures++; }
    new expected2[2] as bytes
    memset(expected2, 0, 2)
    _ = memcpy(expected2, value, 2)
    new actual2[2] as bytes = resize_bytes(value, 2) as bytes
    if (memcmp(actual2, expected2, 2) != 0) { failures++; }
    calls = 0
    actual2 = resize_bytes(identity_bytes(value), 2) as bytes
    if (calls != 1 || memcmp(actual2, expected2, 2) != 0) { failures++; }
    new expected4[4] as bytes
    memset(expected4, 0, 4)
    _ = memcpy(expected4, value, 3)
    new actual4[4] as bytes = resize_bytes(value, 4) as bytes
    if (memcmp(actual4, expected4, 4) != 0) { failures++; }
    calls = 0
    actual4 = resize_bytes(identity_bytes(value), 4) as bytes
    if (calls != 1 || memcmp(actual4, expected4, 4) != 0) { failures++; }
    new expected8[8] as bytes
    memset(expected8, 0, 8)
    _ = memcpy(expected8, value, 3)
    new actual8[8] as bytes = resize_bytes(value, 8) as bytes
    if (memcmp(actual8, expected8, 8) != 0) { failures++; }
    calls = 0
    actual8 = resize_bytes(identity_bytes(value), 8) as bytes
    if (calls != 1 || memcmp(actual8, expected8, 8) != 0) { failures++; }
    return failures
}

func identity_cstr(value[5] as cstr) -> [5] as cstr { calls++; return value; }
func check_cstr(value[5] as cstr) -> i32 {
    new failures as i32 = 0
    new expected1[1] as bytes
    memset(expected1, 0, 1)
    _ = memcpy(expected1, value, 1)
    new actual1[1] as bytes = resize_bytes(value, 1) as bytes
    if (memcmp(actual1, expected1, 1) != 0) { failures++; }
    calls = 0
    actual1 = resize_bytes(identity_cstr(value), 1) as bytes
    if (calls != 1 || memcmp(actual1, expected1, 1) != 0) { failures++; }
    new expected2[2] as bytes
    memset(expected2, 0, 2)
    _ = memcpy(expected2, value, 2)
    new actual2[2] as bytes = resize_bytes(value, 2) as bytes
    if (memcmp(actual2, expected2, 2) != 0) { failures++; }
    calls = 0
    actual2 = resize_bytes(identity_cstr(value), 2) as bytes
    if (calls != 1 || memcmp(actual2, expected2, 2) != 0) { failures++; }
    new expected4[4] as bytes
    memset(expected4, 0, 4)
    _ = memcpy(expected4, value, 4)
    new actual4[4] as bytes = resize_bytes(value, 4) as bytes
    if (memcmp(actual4, expected4, 4) != 0) { failures++; }
    calls = 0
    actual4 = resize_bytes(identity_cstr(value), 4) as bytes
    if (calls != 1 || memcmp(actual4, expected4, 4) != 0) { failures++; }
    new expected8[8] as bytes
    memset(expected8, 0, 8)
    _ = memcpy(expected8, value, 5)
    new actual8[8] as bytes = resize_bytes(value, 8) as bytes
    if (memcmp(actual8, expected8, 8) != 0) { failures++; }
    calls = 0
    actual8 = resize_bytes(identity_cstr(value), 8) as bytes
    if (calls != 1 || memcmp(actual8, expected8, 8) != 0) { failures++; }
    return failures
}

func identity_Packet(value as Packet) -> Packet { calls++; return value; }
func check_Packet(value as Packet) -> i32 {
    new failures as i32 = 0
    new expected1[1] as bytes
    memset(expected1, 0, 1)
    _ = memcpy(expected1, value, 1)
    new actual1[1] as bytes = resize_bytes(value, 1) as bytes
    if (memcmp(actual1, expected1, 1) != 0) { failures++; }
    calls = 0
    actual1 = resize_bytes(identity_Packet(value), 1) as bytes
    if (calls != 1 || memcmp(actual1, expected1, 1) != 0) { failures++; }
    new expected2[2] as bytes
    memset(expected2, 0, 2)
    _ = memcpy(expected2, value, 2)
    new actual2[2] as bytes = resize_bytes(value, 2) as bytes
    if (memcmp(actual2, expected2, 2) != 0) { failures++; }
    calls = 0
    actual2 = resize_bytes(identity_Packet(value), 2) as bytes
    if (calls != 1 || memcmp(actual2, expected2, 2) != 0) { failures++; }
    new expected4[4] as bytes
    memset(expected4, 0, 4)
    _ = memcpy(expected4, value, 4)
    new actual4[4] as bytes = resize_bytes(value, 4) as bytes
    if (memcmp(actual4, expected4, 4) != 0) { failures++; }
    calls = 0
    actual4 = resize_bytes(identity_Packet(value), 4) as bytes
    if (calls != 1 || memcmp(actual4, expected4, 4) != 0) { failures++; }
    new expected8[8] as bytes
    memset(expected8, 0, 8)
    _ = memcpy(expected8, value, 5)
    new actual8[8] as bytes = resize_bytes(value, 8) as bytes
    if (memcmp(actual8, expected8, 8) != 0) { failures++; }
    calls = 0
    actual8 = resize_bytes(identity_Packet(value), 8) as bytes
    if (calls != 1 || memcmp(actual8, expected8, 8) != 0) { failures++; }
    return failures
}

func copy_local() -> [8] as bytes {
    new value as f32 = 1.5
    return resize_bytes(value, 8) as bytes
}
func mutate_shared() -> i32 { shared = 2.5; return 0; }
func inspect_snapshot(value[8] as bytes) -> i32 {
    _ = mutate_shared()
    new expected as f64 = 1.5
    if (memcmp(value, expected, 8) != 0) { return 1; }
    return 0
}

func main() -> i32 {
    new failures as i32 = 0
    new f16_value as f16 = 1.5
    failures = failures + check_f16(f16_value)
    new f16_zero as u16 = 0x8000
    f16_value = f16_zero as f16
    failures = failures + check_f16(f16_value)
    new f16_nan as u16 = 0x7e42
    failures = failures + check_f16(f16_nan as f16)
    new f32_value as f32 = 1.5
    failures = failures + check_f32(f32_value)
    new f32_zero as u32 = 0x80000000
    f32_value = f32_zero as f32
    failures = failures + check_f32(f32_value)
    new f32_nan as u32 = 0x7fc00042
    failures = failures + check_f32(f32_nan as f32)
    new f64_value as f64 = 1.5
    failures = failures + check_f64(f64_value)
    new f64_zero as u64 = 0x8000000000000000
    f64_value = f64_zero as f64
    failures = failures + check_f64(f64_value)
    new f64_nan as u64 = 0x7ff8000000000042
    failures = failures + check_f64(f64_nan as f64)
    new f128_value as f128 = 1.5
    failures = failures + check_f128(f128_value)
    new f128_zero[16] as bytes
    memset(f128_zero, 0, 16)
    f128_zero[15] = 0x80
    f128_value = f128_zero as f128
    failures = failures + check_f128(f128_value)
    new i8_value as i8 = -1
    failures = failures + check_i8(i8_value)
    new i16_value as i16 = -1
    failures = failures + check_i16(i16_value)
    new i32_value as i32 = -1
    failures = failures + check_i32(i32_value)
    new i64_value as i64 = -1
    failures = failures + check_i64(i64_value)
    new u8_value as u8 = 0x81
    failures = failures + check_u8(u8_value)
    new u16_value as u16 = 0x8123
    failures = failures + check_u16(u16_value)
    new u32_value as u32 = 0x81234567
    failures = failures + check_u32(u32_value)
    new u64_value as u64 = 0x8123456789abcdef
    failures = failures + check_u64(u64_value)
    failures = failures + check_bool(true)
    failures = failures + check_addr(&failures)
    new raw[3] as bytes = 'XYZ'
    failures = failures + check_bytes(raw)
    new text[5] as bytes = "ABCD" as bytes
    failures = failures + check_cstr(text as cstr)
    new packet as Packet
    packet.data = "ABCD" as bytes
    failures = failures + check_Packet(packet)
    // Supported native targets are little endian; construct a quiet f128 NaN.
    new wide_nan[16] as bytes
    memset(wide_nan, 0, 16)
    wide_nan[0] = 0x42
    wide_nan[13] = 0x80
    wide_nan[14] = 0xff
    wide_nan[15] = 0x7f
    failures = failures + check_f128(wide_nan as f128)
    new converted_f16 as f16 = 1.5
    new converted_f16_1[1] as bytes
    memset(converted_f16_1, 0, 1)
    _ = memcpy(converted_f16_1, converted_f16, 1)
    if (memcmp(resize_bytes(to_f16(1.5), 1), converted_f16_1, 1) != 0) { failures++; }
    new converted_f16_2[2] as bytes
    memset(converted_f16_2, 0, 2)
    _ = memcpy(converted_f16_2, converted_f16, 2)
    if (memcmp(resize_bytes(to_f16(1.5), 2), converted_f16_2, 2) != 0) { failures++; }
    new converted_f16_4[4] as bytes
    memset(converted_f16_4, 0, 4)
    _ = memcpy(converted_f16_4, converted_f16, 2)
    if (memcmp(resize_bytes(to_f16(1.5), 4), converted_f16_4, 4) != 0) { failures++; }
    new converted_f16_8[8] as bytes
    memset(converted_f16_8, 0, 8)
    _ = memcpy(converted_f16_8, converted_f16, 2)
    if (memcmp(resize_bytes(to_f16(1.5), 8), converted_f16_8, 8) != 0) { failures++; }
    new converted_f32 as f32 = 1.5
    new converted_f32_1[1] as bytes
    memset(converted_f32_1, 0, 1)
    _ = memcpy(converted_f32_1, converted_f32, 1)
    if (memcmp(resize_bytes(to_f32(1.5), 1), converted_f32_1, 1) != 0) { failures++; }
    new converted_f32_2[2] as bytes
    memset(converted_f32_2, 0, 2)
    _ = memcpy(converted_f32_2, converted_f32, 2)
    if (memcmp(resize_bytes(to_f32(1.5), 2), converted_f32_2, 2) != 0) { failures++; }
    new converted_f32_4[4] as bytes
    memset(converted_f32_4, 0, 4)
    _ = memcpy(converted_f32_4, converted_f32, 4)
    if (memcmp(resize_bytes(to_f32(1.5), 4), converted_f32_4, 4) != 0) { failures++; }
    new converted_f32_8[8] as bytes
    memset(converted_f32_8, 0, 8)
    _ = memcpy(converted_f32_8, converted_f32, 4)
    if (memcmp(resize_bytes(to_f32(1.5), 8), converted_f32_8, 8) != 0) { failures++; }
    new converted_f64 as f64 = 1.5
    new converted_f64_1[1] as bytes
    memset(converted_f64_1, 0, 1)
    _ = memcpy(converted_f64_1, converted_f64, 1)
    if (memcmp(resize_bytes(to_f64(1.5), 1), converted_f64_1, 1) != 0) { failures++; }
    new converted_f64_2[2] as bytes
    memset(converted_f64_2, 0, 2)
    _ = memcpy(converted_f64_2, converted_f64, 2)
    if (memcmp(resize_bytes(to_f64(1.5), 2), converted_f64_2, 2) != 0) { failures++; }
    new converted_f64_4[4] as bytes
    memset(converted_f64_4, 0, 4)
    _ = memcpy(converted_f64_4, converted_f64, 4)
    if (memcmp(resize_bytes(to_f64(1.5), 4), converted_f64_4, 4) != 0) { failures++; }
    new converted_f64_8[8] as bytes
    memset(converted_f64_8, 0, 8)
    _ = memcpy(converted_f64_8, converted_f64, 8)
    if (memcmp(resize_bytes(to_f64(1.5), 8), converted_f64_8, 8) != 0) { failures++; }
    new converted_f128 as f128 = 1.5
    new converted_f128_1[1] as bytes
    memset(converted_f128_1, 0, 1)
    _ = memcpy(converted_f128_1, converted_f128, 1)
    if (memcmp(resize_bytes(to_f128(1.5), 1), converted_f128_1, 1) != 0) { failures++; }
    new converted_f128_2[2] as bytes
    memset(converted_f128_2, 0, 2)
    _ = memcpy(converted_f128_2, converted_f128, 2)
    if (memcmp(resize_bytes(to_f128(1.5), 2), converted_f128_2, 2) != 0) { failures++; }
    new converted_f128_4[4] as bytes
    memset(converted_f128_4, 0, 4)
    _ = memcpy(converted_f128_4, converted_f128, 4)
    if (memcmp(resize_bytes(to_f128(1.5), 4), converted_f128_4, 4) != 0) { failures++; }
    new converted_f128_8[8] as bytes
    memset(converted_f128_8, 0, 8)
    _ = memcpy(converted_f128_8, converted_f128, 8)
    if (memcmp(resize_bytes(to_f128(1.5), 8), converted_f128_8, 8) != 0) { failures++; }
    new literal_bits as u64 = 0x8123456789abcdef
    if (memcmp(resize_bytes(0x8123456789abcdef, 1), literal_bits, 1) != 0) { failures++; }
    if (memcmp(resize_bytes(0x8123456789abcdef, 2), literal_bits, 2) != 0) { failures++; }
    if (memcmp(resize_bytes(0x8123456789abcdef, 4), literal_bits, 4) != 0) { failures++; }
    if (memcmp(resize_bytes(0x8123456789abcdef, 8), literal_bits, 8) != 0) { failures++; }
    new expected[8] as bytes
    new source as f64 = 1.5
    new zero as u8 = 0
    if (memcmp(resize_bytes(1.5, 1), source, 1) != 0) { failures++; }
    if (memcmp(resize_bytes(source + source, 1), 3.0, 1) != 0) { failures++; }
    if (memcmp(resize_bytes("A\0Z", 1), resize_bytes("A\0Z", 1 + 0), 1) != 0) { failures++; }
    if (memcmp(resize_bytes(1.5, 2), source, 2) != 0) { failures++; }
    if (memcmp(resize_bytes(source + source, 2), 3.0, 2) != 0) { failures++; }
    if (memcmp(resize_bytes("A\0Z", 2), resize_bytes("A\0Z", 1 + 1), 2) != 0) { failures++; }
    if (memcmp(resize_bytes(1.5, 4), source, 4) != 0) { failures++; }
    if (memcmp(resize_bytes(source + source, 4), 3.0, 4) != 0) { failures++; }
    if (memcmp(resize_bytes("A\0Z", 4), resize_bytes("A\0Z", 1 + 3), 4) != 0) { failures++; }
    if (memcmp(resize_bytes(1.5, 8), source, 8) != 0) { failures++; }
    if (memcmp(resize_bytes(source + source, 8), 3.0, 8) != 0) { failures++; }
    if (memcmp(resize_bytes("A\0Z", 8), resize_bytes("A\0Z", 1 + 7), 8) != 0) { failures++; }
    memset(expected, 0, 8)
    new embedded[4] as bytes = "A\0Z" as bytes
    _ = memcpy(expected, embedded, 4)
    if (memcmp(resize_bytes("A\0Z", 8), expected, 8) != 0) { failures++; }
    if (memcmp(resize_bytes(embedded as cstr, 8), expected, 8) != 0) { failures++; }
    new noncanonical as u8 = 0x7f
    memset(expected, 0, 8)
    _ = memcpy(expected, noncanonical, 1)
    if (memcmp(resize_bytes(noncanonical as bool, 8), expected, 8) != 0) { failures++; }
    memset(expected, 0, 8)
    new minus_one as i8 = -1
    _ = memcpy(expected, minus_one, 1)
    if (memcmp(resize_bytes(-1, 8), expected, 8) != 0) { failures++; }
    _ = memcpy(expected, raw, 3)
    if (memcmp(resize_bytes('XYZ', 8), expected, 8) != 0) { failures++; }
    new f32_value_copy as f32 = 1.5
    memset(expected, 0, 8)
    _ = memcpy(expected, f32_value_copy, 4)
    if (memcmp(copy_local(), expected, 8) != 0) { failures++; }
    new assigned as f64 = 0.0
    if (memcmp(resize_bytes((assigned = 1.5), 8), source, 8) != 0) { failures++; }
    failures = failures + inspect_snapshot(resize_bytes(shared, 8) as bytes)
    if (shared != 2.5) { failures++; }
    calls = 0
    new known[8] as bytes = resize_bytes(source, length(identity_f64(source))) as bytes
    if (calls != 1 || memcmp(known, source, 8) != 0) { failures++; }
    new requested as u64 = 8
    known = resize_bytes(source, requested) as bytes
    if (memcmp(known, source, 8) != 0) { failures++; }
    if (memcmp(resize_bytes(raw[1:3], 2), raw[1:3], 2) != 0) { failures++; }
    if (memcmp(resize_bytes([8]*&source, 8), source, 8) != 0) { failures++; }
    if (memcmp(resize_bytes(resize_bytes(raw, 4), 8), resize_bytes(raw, 8), 8) != 0) { failures++; }
    new prefix[4] as bytes = 'ABCD'
    if (memcmp(resize_bytes([8]*&prefix, 4), prefix, 4) != 0) { failures++; }
    new iteration as i32 = 0
    while (iteration < 4096) {
        if (memcmp(resize_bytes(source, 8), source, 8) != 0) { failures++; }
        iteration++
    }
    print("fixed-resize %d\n", failures)
    return failures
}
