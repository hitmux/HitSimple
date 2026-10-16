$include <ctype.hsh>
$include <stdlib.hsh>
$include <string.hsh>
$include <stdio.hsh>

// Optional ASCII extension oracle: bits lower, upper, xdigit, blank,
// control, print, graph, punct. The independent Python test verifies all
// 256 explicit expected bytes against ASCII membership sets.
new expected[256] as bytes
new calls as i32 = 0

template ThreeBytes { data[3] as bytes; }

func check_bool(actual as bool, mask as u8, bit as u8) -> bool {
    new wanted as bool = (mask & bit) != 0
    new byte as u8 = wanted as u8
    if ((actual as u8) != byte || (actual == true) != wanted || (false != actual) != wanted) { return false; }
    if (to_i32(actual) != to_i32(byte) || to_u64(actual) != to_u64(byte)) { return false; }
    new signed_copy as i32 = actual
    new unsigned_copy as u32 = actual
    if (signed_copy != to_i32(byte) || unsigned_copy != to_u32(byte)) { return false; }
    return true
}

func identity_i8(value as i8) -> i8 { calls++; return value; }
func check_i8(value as i8, mask as u8) -> i32 {
    calls = 0
    if (!check_bool(is_lower(identity_i8(value)), mask, 1) || calls != 1) { return 1; }
    calls = 0
    if (!check_bool(is_upper(identity_i8(value)), mask, 2) || calls != 1) { return 2; }
    calls = 0
    if (!check_bool(is_xdigit(identity_i8(value)), mask, 4) || calls != 1) { return 3; }
    calls = 0
    if (!check_bool(is_blank(identity_i8(value)), mask, 8) || calls != 1) { return 4; }
    calls = 0
    if (!check_bool(is_control(identity_i8(value)), mask, 16) || calls != 1) { return 5; }
    calls = 0
    if (!check_bool(is_print(identity_i8(value)), mask, 32) || calls != 1) { return 6; }
    calls = 0
    if (!check_bool(is_graph(identity_i8(value)), mask, 64) || calls != 1) { return 7; }
    calls = 0
    if (!check_bool(is_punct(identity_i8(value)), mask, 128) || calls != 1) { return 8; }
    return 0
}

func identity_u8(value as u8) -> u8 { calls++; return value; }
func check_u8(value as u8, mask as u8) -> i32 {
    calls = 0
    if (!check_bool(is_lower(identity_u8(value)), mask, 1) || calls != 1) { return 1; }
    calls = 0
    if (!check_bool(is_upper(identity_u8(value)), mask, 2) || calls != 1) { return 2; }
    calls = 0
    if (!check_bool(is_xdigit(identity_u8(value)), mask, 4) || calls != 1) { return 3; }
    calls = 0
    if (!check_bool(is_blank(identity_u8(value)), mask, 8) || calls != 1) { return 4; }
    calls = 0
    if (!check_bool(is_control(identity_u8(value)), mask, 16) || calls != 1) { return 5; }
    calls = 0
    if (!check_bool(is_print(identity_u8(value)), mask, 32) || calls != 1) { return 6; }
    calls = 0
    if (!check_bool(is_graph(identity_u8(value)), mask, 64) || calls != 1) { return 7; }
    calls = 0
    if (!check_bool(is_punct(identity_u8(value)), mask, 128) || calls != 1) { return 8; }
    return 0
}

func identity_i16(value as i16) -> i16 { calls++; return value; }
func check_i16(value as i16, mask as u8) -> i32 {
    calls = 0
    if (!check_bool(is_lower(identity_i16(value)), mask, 1) || calls != 1) { return 1; }
    calls = 0
    if (!check_bool(is_upper(identity_i16(value)), mask, 2) || calls != 1) { return 2; }
    calls = 0
    if (!check_bool(is_xdigit(identity_i16(value)), mask, 4) || calls != 1) { return 3; }
    calls = 0
    if (!check_bool(is_blank(identity_i16(value)), mask, 8) || calls != 1) { return 4; }
    calls = 0
    if (!check_bool(is_control(identity_i16(value)), mask, 16) || calls != 1) { return 5; }
    calls = 0
    if (!check_bool(is_print(identity_i16(value)), mask, 32) || calls != 1) { return 6; }
    calls = 0
    if (!check_bool(is_graph(identity_i16(value)), mask, 64) || calls != 1) { return 7; }
    calls = 0
    if (!check_bool(is_punct(identity_i16(value)), mask, 128) || calls != 1) { return 8; }
    return 0
}

func identity_u16(value as u16) -> u16 { calls++; return value; }
func check_u16(value as u16, mask as u8) -> i32 {
    calls = 0
    if (!check_bool(is_lower(identity_u16(value)), mask, 1) || calls != 1) { return 1; }
    calls = 0
    if (!check_bool(is_upper(identity_u16(value)), mask, 2) || calls != 1) { return 2; }
    calls = 0
    if (!check_bool(is_xdigit(identity_u16(value)), mask, 4) || calls != 1) { return 3; }
    calls = 0
    if (!check_bool(is_blank(identity_u16(value)), mask, 8) || calls != 1) { return 4; }
    calls = 0
    if (!check_bool(is_control(identity_u16(value)), mask, 16) || calls != 1) { return 5; }
    calls = 0
    if (!check_bool(is_print(identity_u16(value)), mask, 32) || calls != 1) { return 6; }
    calls = 0
    if (!check_bool(is_graph(identity_u16(value)), mask, 64) || calls != 1) { return 7; }
    calls = 0
    if (!check_bool(is_punct(identity_u16(value)), mask, 128) || calls != 1) { return 8; }
    return 0
}

func identity_i32(value as i32) -> i32 { calls++; return value; }
func check_i32(value as i32, mask as u8) -> i32 {
    calls = 0
    if (!check_bool(is_lower(identity_i32(value)), mask, 1) || calls != 1) { return 1; }
    calls = 0
    if (!check_bool(is_upper(identity_i32(value)), mask, 2) || calls != 1) { return 2; }
    calls = 0
    if (!check_bool(is_xdigit(identity_i32(value)), mask, 4) || calls != 1) { return 3; }
    calls = 0
    if (!check_bool(is_blank(identity_i32(value)), mask, 8) || calls != 1) { return 4; }
    calls = 0
    if (!check_bool(is_control(identity_i32(value)), mask, 16) || calls != 1) { return 5; }
    calls = 0
    if (!check_bool(is_print(identity_i32(value)), mask, 32) || calls != 1) { return 6; }
    calls = 0
    if (!check_bool(is_graph(identity_i32(value)), mask, 64) || calls != 1) { return 7; }
    calls = 0
    if (!check_bool(is_punct(identity_i32(value)), mask, 128) || calls != 1) { return 8; }
    return 0
}

func identity_u32(value as u32) -> u32 { calls++; return value; }
func check_u32(value as u32, mask as u8) -> i32 {
    calls = 0
    if (!check_bool(is_lower(identity_u32(value)), mask, 1) || calls != 1) { return 1; }
    calls = 0
    if (!check_bool(is_upper(identity_u32(value)), mask, 2) || calls != 1) { return 2; }
    calls = 0
    if (!check_bool(is_xdigit(identity_u32(value)), mask, 4) || calls != 1) { return 3; }
    calls = 0
    if (!check_bool(is_blank(identity_u32(value)), mask, 8) || calls != 1) { return 4; }
    calls = 0
    if (!check_bool(is_control(identity_u32(value)), mask, 16) || calls != 1) { return 5; }
    calls = 0
    if (!check_bool(is_print(identity_u32(value)), mask, 32) || calls != 1) { return 6; }
    calls = 0
    if (!check_bool(is_graph(identity_u32(value)), mask, 64) || calls != 1) { return 7; }
    calls = 0
    if (!check_bool(is_punct(identity_u32(value)), mask, 128) || calls != 1) { return 8; }
    return 0
}

func identity_i64(value as i64) -> i64 { calls++; return value; }
func check_i64(value as i64, mask as u8) -> i32 {
    calls = 0
    if (!check_bool(is_lower(identity_i64(value)), mask, 1) || calls != 1) { return 1; }
    calls = 0
    if (!check_bool(is_upper(identity_i64(value)), mask, 2) || calls != 1) { return 2; }
    calls = 0
    if (!check_bool(is_xdigit(identity_i64(value)), mask, 4) || calls != 1) { return 3; }
    calls = 0
    if (!check_bool(is_blank(identity_i64(value)), mask, 8) || calls != 1) { return 4; }
    calls = 0
    if (!check_bool(is_control(identity_i64(value)), mask, 16) || calls != 1) { return 5; }
    calls = 0
    if (!check_bool(is_print(identity_i64(value)), mask, 32) || calls != 1) { return 6; }
    calls = 0
    if (!check_bool(is_graph(identity_i64(value)), mask, 64) || calls != 1) { return 7; }
    calls = 0
    if (!check_bool(is_punct(identity_i64(value)), mask, 128) || calls != 1) { return 8; }
    return 0
}

func identity_u64(value as u64) -> u64 { calls++; return value; }
func check_u64(value as u64, mask as u8) -> i32 {
    calls = 0
    if (!check_bool(is_lower(identity_u64(value)), mask, 1) || calls != 1) { return 1; }
    calls = 0
    if (!check_bool(is_upper(identity_u64(value)), mask, 2) || calls != 1) { return 2; }
    calls = 0
    if (!check_bool(is_xdigit(identity_u64(value)), mask, 4) || calls != 1) { return 3; }
    calls = 0
    if (!check_bool(is_blank(identity_u64(value)), mask, 8) || calls != 1) { return 4; }
    calls = 0
    if (!check_bool(is_control(identity_u64(value)), mask, 16) || calls != 1) { return 5; }
    calls = 0
    if (!check_bool(is_print(identity_u64(value)), mask, 32) || calls != 1) { return 6; }
    calls = 0
    if (!check_bool(is_graph(identity_u64(value)), mask, 64) || calls != 1) { return 7; }
    calls = 0
    if (!check_bool(is_punct(identity_u64(value)), mask, 128) || calls != 1) { return 8; }
    return 0
}

func identity_f16(value as f16) -> f16 { calls++; return value; }
func check_f16(value as f16, mask as u8) -> i32 {
    calls = 0
    if (!check_bool(is_lower(identity_f16(value)), mask, 1) || calls != 1) { return 1; }
    calls = 0
    if (!check_bool(is_upper(identity_f16(value)), mask, 2) || calls != 1) { return 2; }
    calls = 0
    if (!check_bool(is_xdigit(identity_f16(value)), mask, 4) || calls != 1) { return 3; }
    calls = 0
    if (!check_bool(is_blank(identity_f16(value)), mask, 8) || calls != 1) { return 4; }
    calls = 0
    if (!check_bool(is_control(identity_f16(value)), mask, 16) || calls != 1) { return 5; }
    calls = 0
    if (!check_bool(is_print(identity_f16(value)), mask, 32) || calls != 1) { return 6; }
    calls = 0
    if (!check_bool(is_graph(identity_f16(value)), mask, 64) || calls != 1) { return 7; }
    calls = 0
    if (!check_bool(is_punct(identity_f16(value)), mask, 128) || calls != 1) { return 8; }
    return 0
}

func identity_f32(value as f32) -> f32 { calls++; return value; }
func check_f32(value as f32, mask as u8) -> i32 {
    calls = 0
    if (!check_bool(is_lower(identity_f32(value)), mask, 1) || calls != 1) { return 1; }
    calls = 0
    if (!check_bool(is_upper(identity_f32(value)), mask, 2) || calls != 1) { return 2; }
    calls = 0
    if (!check_bool(is_xdigit(identity_f32(value)), mask, 4) || calls != 1) { return 3; }
    calls = 0
    if (!check_bool(is_blank(identity_f32(value)), mask, 8) || calls != 1) { return 4; }
    calls = 0
    if (!check_bool(is_control(identity_f32(value)), mask, 16) || calls != 1) { return 5; }
    calls = 0
    if (!check_bool(is_print(identity_f32(value)), mask, 32) || calls != 1) { return 6; }
    calls = 0
    if (!check_bool(is_graph(identity_f32(value)), mask, 64) || calls != 1) { return 7; }
    calls = 0
    if (!check_bool(is_punct(identity_f32(value)), mask, 128) || calls != 1) { return 8; }
    return 0
}

func identity_f64(value as f64) -> f64 { calls++; return value; }
func check_f64(value as f64, mask as u8) -> i32 {
    calls = 0
    if (!check_bool(is_lower(identity_f64(value)), mask, 1) || calls != 1) { return 1; }
    calls = 0
    if (!check_bool(is_upper(identity_f64(value)), mask, 2) || calls != 1) { return 2; }
    calls = 0
    if (!check_bool(is_xdigit(identity_f64(value)), mask, 4) || calls != 1) { return 3; }
    calls = 0
    if (!check_bool(is_blank(identity_f64(value)), mask, 8) || calls != 1) { return 4; }
    calls = 0
    if (!check_bool(is_control(identity_f64(value)), mask, 16) || calls != 1) { return 5; }
    calls = 0
    if (!check_bool(is_print(identity_f64(value)), mask, 32) || calls != 1) { return 6; }
    calls = 0
    if (!check_bool(is_graph(identity_f64(value)), mask, 64) || calls != 1) { return 7; }
    calls = 0
    if (!check_bool(is_punct(identity_f64(value)), mask, 128) || calls != 1) { return 8; }
    return 0
}

func identity_f128(value as f128) -> f128 { calls++; return value; }
func check_f128(value as f128, mask as u8) -> i32 {
    calls = 0
    if (!check_bool(is_lower(identity_f128(value)), mask, 1) || calls != 1) { return 1; }
    calls = 0
    if (!check_bool(is_upper(identity_f128(value)), mask, 2) || calls != 1) { return 2; }
    calls = 0
    if (!check_bool(is_xdigit(identity_f128(value)), mask, 4) || calls != 1) { return 3; }
    calls = 0
    if (!check_bool(is_blank(identity_f128(value)), mask, 8) || calls != 1) { return 4; }
    calls = 0
    if (!check_bool(is_control(identity_f128(value)), mask, 16) || calls != 1) { return 5; }
    calls = 0
    if (!check_bool(is_print(identity_f128(value)), mask, 32) || calls != 1) { return 6; }
    calls = 0
    if (!check_bool(is_graph(identity_f128(value)), mask, 64) || calls != 1) { return 7; }
    calls = 0
    if (!check_bool(is_punct(identity_f128(value)), mask, 128) || calls != 1) { return 8; }
    return 0
}

func identity_bool_view(value as bool) -> u8 { calls++; return value as u8; }
func check_bool_view(value as bool, mask as u8) -> i32 {
    calls = 0
    if (!check_bool(is_lower(identity_bool_view(value)), mask, 1) || calls != 1) { return 1; }
    calls = 0
    if (!check_bool(is_upper(identity_bool_view(value)), mask, 2) || calls != 1) { return 2; }
    calls = 0
    if (!check_bool(is_xdigit(identity_bool_view(value)), mask, 4) || calls != 1) { return 3; }
    calls = 0
    if (!check_bool(is_blank(identity_bool_view(value)), mask, 8) || calls != 1) { return 4; }
    calls = 0
    if (!check_bool(is_control(identity_bool_view(value)), mask, 16) || calls != 1) { return 5; }
    calls = 0
    if (!check_bool(is_print(identity_bool_view(value)), mask, 32) || calls != 1) { return 6; }
    calls = 0
    if (!check_bool(is_graph(identity_bool_view(value)), mask, 64) || calls != 1) { return 7; }
    calls = 0
    if (!check_bool(is_punct(identity_bool_view(value)), mask, 128) || calls != 1) { return 8; }
    if (!check_bool(is_lower(value), mask, 1)) { return 1; }
    if (!check_bool(is_upper(value), mask, 2)) { return 2; }
    if (!check_bool(is_xdigit(value), mask, 4)) { return 3; }
    if (!check_bool(is_blank(value), mask, 8)) { return 4; }
    if (!check_bool(is_control(value), mask, 16)) { return 5; }
    if (!check_bool(is_print(value), mask, 32)) { return 6; }
    if (!check_bool(is_graph(value), mask, 64)) { return 7; }
    if (!check_bool(is_punct(value), mask, 128)) { return 8; }
    return 0
}

func identity_addr(value as addr) -> addr { calls++; return value; }
func check_addr(value as addr, mask as u8) -> i32 {
    calls = 0
    if (!check_bool(is_lower(identity_addr(value)), mask, 1) || calls != 1) { return 1; }
    calls = 0
    if (!check_bool(is_upper(identity_addr(value)), mask, 2) || calls != 1) { return 2; }
    calls = 0
    if (!check_bool(is_xdigit(identity_addr(value)), mask, 4) || calls != 1) { return 3; }
    calls = 0
    if (!check_bool(is_blank(identity_addr(value)), mask, 8) || calls != 1) { return 4; }
    calls = 0
    if (!check_bool(is_control(identity_addr(value)), mask, 16) || calls != 1) { return 5; }
    calls = 0
    if (!check_bool(is_print(identity_addr(value)), mask, 32) || calls != 1) { return 6; }
    calls = 0
    if (!check_bool(is_graph(identity_addr(value)), mask, 64) || calls != 1) { return 7; }
    calls = 0
    if (!check_bool(is_punct(identity_addr(value)), mask, 128) || calls != 1) { return 8; }
    return 0
}

func identity_bytes_1(value[1] as bytes) -> [1] as bytes { calls++; return value; }
func check_bytes_1(value[1] as bytes, mask as u8) -> i32 {
    calls = 0
    if (!check_bool(is_lower(identity_bytes_1(value)), mask, 1) || calls != 1) { return 1; }
    calls = 0
    if (!check_bool(is_upper(identity_bytes_1(value)), mask, 2) || calls != 1) { return 2; }
    calls = 0
    if (!check_bool(is_xdigit(identity_bytes_1(value)), mask, 4) || calls != 1) { return 3; }
    calls = 0
    if (!check_bool(is_blank(identity_bytes_1(value)), mask, 8) || calls != 1) { return 4; }
    calls = 0
    if (!check_bool(is_control(identity_bytes_1(value)), mask, 16) || calls != 1) { return 5; }
    calls = 0
    if (!check_bool(is_print(identity_bytes_1(value)), mask, 32) || calls != 1) { return 6; }
    calls = 0
    if (!check_bool(is_graph(identity_bytes_1(value)), mask, 64) || calls != 1) { return 7; }
    calls = 0
    if (!check_bool(is_punct(identity_bytes_1(value)), mask, 128) || calls != 1) { return 8; }
    return 0
}

func identity_bytes_3(value[3] as bytes) -> [3] as bytes { calls++; return value; }
func check_bytes_3(value[3] as bytes, mask as u8) -> i32 {
    calls = 0
    if (!check_bool(is_lower(identity_bytes_3(value)), mask, 1) || calls != 1) { return 1; }
    calls = 0
    if (!check_bool(is_upper(identity_bytes_3(value)), mask, 2) || calls != 1) { return 2; }
    calls = 0
    if (!check_bool(is_xdigit(identity_bytes_3(value)), mask, 4) || calls != 1) { return 3; }
    calls = 0
    if (!check_bool(is_blank(identity_bytes_3(value)), mask, 8) || calls != 1) { return 4; }
    calls = 0
    if (!check_bool(is_control(identity_bytes_3(value)), mask, 16) || calls != 1) { return 5; }
    calls = 0
    if (!check_bool(is_print(identity_bytes_3(value)), mask, 32) || calls != 1) { return 6; }
    calls = 0
    if (!check_bool(is_graph(identity_bytes_3(value)), mask, 64) || calls != 1) { return 7; }
    calls = 0
    if (!check_bool(is_punct(identity_bytes_3(value)), mask, 128) || calls != 1) { return 8; }
    return 0
}

func identity_bytes_32(value[32] as bytes) -> [32] as bytes { calls++; return value; }
func check_bytes_32(value[32] as bytes, mask as u8) -> i32 {
    calls = 0
    if (!check_bool(is_lower(identity_bytes_32(value)), mask, 1) || calls != 1) { return 1; }
    calls = 0
    if (!check_bool(is_upper(identity_bytes_32(value)), mask, 2) || calls != 1) { return 2; }
    calls = 0
    if (!check_bool(is_xdigit(identity_bytes_32(value)), mask, 4) || calls != 1) { return 3; }
    calls = 0
    if (!check_bool(is_blank(identity_bytes_32(value)), mask, 8) || calls != 1) { return 4; }
    calls = 0
    if (!check_bool(is_control(identity_bytes_32(value)), mask, 16) || calls != 1) { return 5; }
    calls = 0
    if (!check_bool(is_print(identity_bytes_32(value)), mask, 32) || calls != 1) { return 6; }
    calls = 0
    if (!check_bool(is_graph(identity_bytes_32(value)), mask, 64) || calls != 1) { return 7; }
    calls = 0
    if (!check_bool(is_punct(identity_bytes_32(value)), mask, 128) || calls != 1) { return 8; }
    return 0
}

func identity_none_3(value[3] as none) -> [3] as none { calls++; return value; }
func check_none_3(value[3] as none, mask as u8) -> i32 {
    calls = 0
    if (!check_bool(is_lower(identity_none_3(value)), mask, 1) || calls != 1) { return 1; }
    calls = 0
    if (!check_bool(is_upper(identity_none_3(value)), mask, 2) || calls != 1) { return 2; }
    calls = 0
    if (!check_bool(is_xdigit(identity_none_3(value)), mask, 4) || calls != 1) { return 3; }
    calls = 0
    if (!check_bool(is_blank(identity_none_3(value)), mask, 8) || calls != 1) { return 4; }
    calls = 0
    if (!check_bool(is_control(identity_none_3(value)), mask, 16) || calls != 1) { return 5; }
    calls = 0
    if (!check_bool(is_print(identity_none_3(value)), mask, 32) || calls != 1) { return 6; }
    calls = 0
    if (!check_bool(is_graph(identity_none_3(value)), mask, 64) || calls != 1) { return 7; }
    calls = 0
    if (!check_bool(is_punct(identity_none_3(value)), mask, 128) || calls != 1) { return 8; }
    return 0
}

func identity_cstr_3(value[3] as cstr) -> [3] as cstr { calls++; return value; }
func check_cstr_3(value[3] as cstr, mask as u8) -> i32 {
    calls = 0
    if (!check_bool(is_lower(identity_cstr_3(value)), mask, 1) || calls != 1) { return 1; }
    calls = 0
    if (!check_bool(is_upper(identity_cstr_3(value)), mask, 2) || calls != 1) { return 2; }
    calls = 0
    if (!check_bool(is_xdigit(identity_cstr_3(value)), mask, 4) || calls != 1) { return 3; }
    calls = 0
    if (!check_bool(is_blank(identity_cstr_3(value)), mask, 8) || calls != 1) { return 4; }
    calls = 0
    if (!check_bool(is_control(identity_cstr_3(value)), mask, 16) || calls != 1) { return 5; }
    calls = 0
    if (!check_bool(is_print(identity_cstr_3(value)), mask, 32) || calls != 1) { return 6; }
    calls = 0
    if (!check_bool(is_graph(identity_cstr_3(value)), mask, 64) || calls != 1) { return 7; }
    calls = 0
    if (!check_bool(is_punct(identity_cstr_3(value)), mask, 128) || calls != 1) { return 8; }
    return 0
}

func identity_template(value as ThreeBytes) -> ThreeBytes { calls++; return value; }
func check_template(value as ThreeBytes, mask as u8) -> i32 {
    calls = 0
    if (!check_bool(is_lower(identity_template(value)), mask, 1) || calls != 1) { return 1; }
    calls = 0
    if (!check_bool(is_upper(identity_template(value)), mask, 2) || calls != 1) { return 2; }
    calls = 0
    if (!check_bool(is_xdigit(identity_template(value)), mask, 4) || calls != 1) { return 3; }
    calls = 0
    if (!check_bool(is_blank(identity_template(value)), mask, 8) || calls != 1) { return 4; }
    calls = 0
    if (!check_bool(is_control(identity_template(value)), mask, 16) || calls != 1) { return 5; }
    calls = 0
    if (!check_bool(is_print(identity_template(value)), mask, 32) || calls != 1) { return 6; }
    calls = 0
    if (!check_bool(is_graph(identity_template(value)), mask, 64) || calls != 1) { return 7; }
    calls = 0
    if (!check_bool(is_punct(identity_template(value)), mask, 128) || calls != 1) { return 8; }
    return 0
}

func check_expressions(value as f128, mask as u8) -> i32 {
    new count as u64 = 1
    if (!check_bool(is_lower(true ? value : value), mask, 1)) { return 1; }
    if (!check_bool(is_lower(value = value), mask, 1)) { return 1; }
    if (!check_bool(is_lower(resize_bytes(value, count)), mask, 1)) { return 1; }
    if (!check_bool(is_lower(value[0:1]), mask, 1)) { return 1; }
    if (!check_bool(is_lower([1]*(&value)), mask, 1)) { return 1; }
    if (!check_bool(is_upper(true ? value : value), mask, 2)) { return 2; }
    if (!check_bool(is_upper(value = value), mask, 2)) { return 2; }
    if (!check_bool(is_upper(resize_bytes(value, count)), mask, 2)) { return 2; }
    if (!check_bool(is_upper(value[0:1]), mask, 2)) { return 2; }
    if (!check_bool(is_upper([1]*(&value)), mask, 2)) { return 2; }
    if (!check_bool(is_xdigit(true ? value : value), mask, 4)) { return 3; }
    if (!check_bool(is_xdigit(value = value), mask, 4)) { return 3; }
    if (!check_bool(is_xdigit(resize_bytes(value, count)), mask, 4)) { return 3; }
    if (!check_bool(is_xdigit(value[0:1]), mask, 4)) { return 3; }
    if (!check_bool(is_xdigit([1]*(&value)), mask, 4)) { return 3; }
    if (!check_bool(is_blank(true ? value : value), mask, 8)) { return 4; }
    if (!check_bool(is_blank(value = value), mask, 8)) { return 4; }
    if (!check_bool(is_blank(resize_bytes(value, count)), mask, 8)) { return 4; }
    if (!check_bool(is_blank(value[0:1]), mask, 8)) { return 4; }
    if (!check_bool(is_blank([1]*(&value)), mask, 8)) { return 4; }
    if (!check_bool(is_control(true ? value : value), mask, 16)) { return 5; }
    if (!check_bool(is_control(value = value), mask, 16)) { return 5; }
    if (!check_bool(is_control(resize_bytes(value, count)), mask, 16)) { return 5; }
    if (!check_bool(is_control(value[0:1]), mask, 16)) { return 5; }
    if (!check_bool(is_control([1]*(&value)), mask, 16)) { return 5; }
    if (!check_bool(is_print(true ? value : value), mask, 32)) { return 6; }
    if (!check_bool(is_print(value = value), mask, 32)) { return 6; }
    if (!check_bool(is_print(resize_bytes(value, count)), mask, 32)) { return 6; }
    if (!check_bool(is_print(value[0:1]), mask, 32)) { return 6; }
    if (!check_bool(is_print([1]*(&value)), mask, 32)) { return 6; }
    if (!check_bool(is_graph(true ? value : value), mask, 64)) { return 7; }
    if (!check_bool(is_graph(value = value), mask, 64)) { return 7; }
    if (!check_bool(is_graph(resize_bytes(value, count)), mask, 64)) { return 7; }
    if (!check_bool(is_graph(value[0:1]), mask, 64)) { return 7; }
    if (!check_bool(is_graph([1]*(&value)), mask, 64)) { return 7; }
    if (!check_bool(is_punct(true ? value : value), mask, 128)) { return 8; }
    if (!check_bool(is_punct(value = value), mask, 128)) { return 8; }
    if (!check_bool(is_punct(resize_bytes(value, count)), mask, 128)) { return 8; }
    if (!check_bool(is_punct(value[0:1]), mask, 128)) { return 8; }
    if (!check_bool(is_punct([1]*(&value)), mask, 128)) { return 8; }
    return 0
}

func main() -> i32 {
    expected[0] %d= 16
    expected[1] %d= 16
    expected[2] %d= 16
    expected[3] %d= 16
    expected[4] %d= 16
    expected[5] %d= 16
    expected[6] %d= 16
    expected[7] %d= 16
    expected[8] %d= 16
    expected[9] %d= 24
    expected[10] %d= 16
    expected[11] %d= 16
    expected[12] %d= 16
    expected[13] %d= 16
    expected[14] %d= 16
    expected[15] %d= 16
    expected[16] %d= 16
    expected[17] %d= 16
    expected[18] %d= 16
    expected[19] %d= 16
    expected[20] %d= 16
    expected[21] %d= 16
    expected[22] %d= 16
    expected[23] %d= 16
    expected[24] %d= 16
    expected[25] %d= 16
    expected[26] %d= 16
    expected[27] %d= 16
    expected[28] %d= 16
    expected[29] %d= 16
    expected[30] %d= 16
    expected[31] %d= 16
    expected[32] %d= 40
    expected[33] %d= 224
    expected[34] %d= 224
    expected[35] %d= 224
    expected[36] %d= 224
    expected[37] %d= 224
    expected[38] %d= 224
    expected[39] %d= 224
    expected[40] %d= 224
    expected[41] %d= 224
    expected[42] %d= 224
    expected[43] %d= 224
    expected[44] %d= 224
    expected[45] %d= 224
    expected[46] %d= 224
    expected[47] %d= 224
    expected[48] %d= 100
    expected[49] %d= 100
    expected[50] %d= 100
    expected[51] %d= 100
    expected[52] %d= 100
    expected[53] %d= 100
    expected[54] %d= 100
    expected[55] %d= 100
    expected[56] %d= 100
    expected[57] %d= 100
    expected[58] %d= 224
    expected[59] %d= 224
    expected[60] %d= 224
    expected[61] %d= 224
    expected[62] %d= 224
    expected[63] %d= 224
    expected[64] %d= 224
    expected[65] %d= 102
    expected[66] %d= 102
    expected[67] %d= 102
    expected[68] %d= 102
    expected[69] %d= 102
    expected[70] %d= 102
    expected[71] %d= 98
    expected[72] %d= 98
    expected[73] %d= 98
    expected[74] %d= 98
    expected[75] %d= 98
    expected[76] %d= 98
    expected[77] %d= 98
    expected[78] %d= 98
    expected[79] %d= 98
    expected[80] %d= 98
    expected[81] %d= 98
    expected[82] %d= 98
    expected[83] %d= 98
    expected[84] %d= 98
    expected[85] %d= 98
    expected[86] %d= 98
    expected[87] %d= 98
    expected[88] %d= 98
    expected[89] %d= 98
    expected[90] %d= 98
    expected[91] %d= 224
    expected[92] %d= 224
    expected[93] %d= 224
    expected[94] %d= 224
    expected[95] %d= 224
    expected[96] %d= 224
    expected[97] %d= 101
    expected[98] %d= 101
    expected[99] %d= 101
    expected[100] %d= 101
    expected[101] %d= 101
    expected[102] %d= 101
    expected[103] %d= 97
    expected[104] %d= 97
    expected[105] %d= 97
    expected[106] %d= 97
    expected[107] %d= 97
    expected[108] %d= 97
    expected[109] %d= 97
    expected[110] %d= 97
    expected[111] %d= 97
    expected[112] %d= 97
    expected[113] %d= 97
    expected[114] %d= 97
    expected[115] %d= 97
    expected[116] %d= 97
    expected[117] %d= 97
    expected[118] %d= 97
    expected[119] %d= 97
    expected[120] %d= 97
    expected[121] %d= 97
    expected[122] %d= 97
    expected[123] %d= 224
    expected[124] %d= 224
    expected[125] %d= 224
    expected[126] %d= 224
    expected[127] %d= 16
    expected[128] %d= 0
    expected[129] %d= 0
    expected[130] %d= 0
    expected[131] %d= 0
    expected[132] %d= 0
    expected[133] %d= 0
    expected[134] %d= 0
    expected[135] %d= 0
    expected[136] %d= 0
    expected[137] %d= 0
    expected[138] %d= 0
    expected[139] %d= 0
    expected[140] %d= 0
    expected[141] %d= 0
    expected[142] %d= 0
    expected[143] %d= 0
    expected[144] %d= 0
    expected[145] %d= 0
    expected[146] %d= 0
    expected[147] %d= 0
    expected[148] %d= 0
    expected[149] %d= 0
    expected[150] %d= 0
    expected[151] %d= 0
    expected[152] %d= 0
    expected[153] %d= 0
    expected[154] %d= 0
    expected[155] %d= 0
    expected[156] %d= 0
    expected[157] %d= 0
    expected[158] %d= 0
    expected[159] %d= 0
    expected[160] %d= 0
    expected[161] %d= 0
    expected[162] %d= 0
    expected[163] %d= 0
    expected[164] %d= 0
    expected[165] %d= 0
    expected[166] %d= 0
    expected[167] %d= 0
    expected[168] %d= 0
    expected[169] %d= 0
    expected[170] %d= 0
    expected[171] %d= 0
    expected[172] %d= 0
    expected[173] %d= 0
    expected[174] %d= 0
    expected[175] %d= 0
    expected[176] %d= 0
    expected[177] %d= 0
    expected[178] %d= 0
    expected[179] %d= 0
    expected[180] %d= 0
    expected[181] %d= 0
    expected[182] %d= 0
    expected[183] %d= 0
    expected[184] %d= 0
    expected[185] %d= 0
    expected[186] %d= 0
    expected[187] %d= 0
    expected[188] %d= 0
    expected[189] %d= 0
    expected[190] %d= 0
    expected[191] %d= 0
    expected[192] %d= 0
    expected[193] %d= 0
    expected[194] %d= 0
    expected[195] %d= 0
    expected[196] %d= 0
    expected[197] %d= 0
    expected[198] %d= 0
    expected[199] %d= 0
    expected[200] %d= 0
    expected[201] %d= 0
    expected[202] %d= 0
    expected[203] %d= 0
    expected[204] %d= 0
    expected[205] %d= 0
    expected[206] %d= 0
    expected[207] %d= 0
    expected[208] %d= 0
    expected[209] %d= 0
    expected[210] %d= 0
    expected[211] %d= 0
    expected[212] %d= 0
    expected[213] %d= 0
    expected[214] %d= 0
    expected[215] %d= 0
    expected[216] %d= 0
    expected[217] %d= 0
    expected[218] %d= 0
    expected[219] %d= 0
    expected[220] %d= 0
    expected[221] %d= 0
    expected[222] %d= 0
    expected[223] %d= 0
    expected[224] %d= 0
    expected[225] %d= 0
    expected[226] %d= 0
    expected[227] %d= 0
    expected[228] %d= 0
    expected[229] %d= 0
    expected[230] %d= 0
    expected[231] %d= 0
    expected[232] %d= 0
    expected[233] %d= 0
    expected[234] %d= 0
    expected[235] %d= 0
    expected[236] %d= 0
    expected[237] %d= 0
    expected[238] %d= 0
    expected[239] %d= 0
    expected[240] %d= 0
    expected[241] %d= 0
    expected[242] %d= 0
    expected[243] %d= 0
    expected[244] %d= 0
    expected[245] %d= 0
    expected[246] %d= 0
    expected[247] %d= 0
    expected[248] %d= 0
    expected[249] %d= 0
    expected[250] %d= 0
    expected[251] %d= 0
    expected[252] %d= 0
    expected[253] %d= 0
    expected[254] %d= 0
    expected[255] %d= 0
    new failures as i32 = 0
    new raw[32] as bytes
    new prefix as i32 = 0
    while (prefix < 2) {
        memset(raw, 0, 32)
        if (prefix != 0) { memset(raw, 165, 32); }
        new byte as i32 = 0
        while (byte < 256) {
            raw[0] %d= byte
            new mask as u8 = expected[byte]
            new value_i8 as i8
            new copied_i8 as addr = memcpy(value_i8, raw, 1)
            if (check_i8(value_i8, mask) != 0) { failures++; }
            new value_u8 as u8
            new copied_u8 as addr = memcpy(value_u8, raw, 1)
            if (check_u8(value_u8, mask) != 0) { failures++; }
            new value_i16 as i16
            new copied_i16 as addr = memcpy(value_i16, raw, 2)
            if (check_i16(value_i16, mask) != 0) { failures++; }
            new value_u16 as u16
            new copied_u16 as addr = memcpy(value_u16, raw, 2)
            if (check_u16(value_u16, mask) != 0) { failures++; }
            new value_i32 as i32
            new copied_i32 as addr = memcpy(value_i32, raw, 4)
            if (check_i32(value_i32, mask) != 0) { failures++; }
            new value_u32 as u32
            new copied_u32 as addr = memcpy(value_u32, raw, 4)
            if (check_u32(value_u32, mask) != 0) { failures++; }
            new value_i64 as i64
            new copied_i64 as addr = memcpy(value_i64, raw, 8)
            if (check_i64(value_i64, mask) != 0) { failures++; }
            new value_u64 as u64
            new copied_u64 as addr = memcpy(value_u64, raw, 8)
            if (check_u64(value_u64, mask) != 0) { failures++; }
            new value_f16 as f16
            new copied_f16 as addr = memcpy(value_f16, raw, 2)
            if (check_f16(value_f16, mask) != 0) { failures++; }
            new value_f32 as f32
            new copied_f32 as addr = memcpy(value_f32, raw, 4)
            if (check_f32(value_f32, mask) != 0) { failures++; }
            new value_f64 as f64
            new copied_f64 as addr = memcpy(value_f64, raw, 8)
            if (check_f64(value_f64, mask) != 0) { failures++; }
            new value_f128 as f128
            new copied_f128 as addr = memcpy(value_f128, raw, 16)
            if (check_f128(value_f128, mask) != 0) { failures++; }
            new value_bool as bool
            new copied_bool as addr = memcpy(value_bool, raw, 1)
            if (check_bool_view(value_bool, mask) != 0) { failures++; }
            new value_addr as addr
            new copied_addr as addr = memcpy(value_addr as bytes, raw, length(value_addr))
            if (check_addr(value_addr, mask) != 0) { failures++; }
            new small_1[1] as bytes = raw[0:1]
            if (check_bytes_1(small_1, mask) != 0) { failures++; }
            new small_3[3] as bytes = raw[0:3]
            if (check_bytes_3(small_3, mask) != 0) { failures++; }
            if (check_bytes_32(raw, mask) != 0) { failures++; }
            if (check_none_3(small_3 as none, mask) != 0) { failures++; }
            new text[3] as cstr
            new copied_text as addr = memcpy(text, raw, 3)
            text[2] %d= 0
            if (check_cstr_3(text, mask) != 0) { failures++; }
            if (check_template(small_3 as ThreeBytes, mask) != 0) { failures++; }
            if (check_expressions(value_f128, mask) != 0) { failures++; }
            if (check_i32(byte + 256, mask) != 0 || check_i32(byte - 256, mask) != 0) { failures++; }
            if (check_i32(byte + 0x123400, mask) != 0 || check_i32(byte + 0x7fffff00, mask) != 0) { failures++; }
            if (check_i32(byte - 2147483647 - 1, mask) != 0) { failures++; }
            new wide as i64 = to_i64(byte) + 0x123456789abcde00
            if (check_i64(wide, mask) != 0) { failures++; }
            new negative as i64 = to_i64(byte) - 9223372036854775807 - 1
            if (check_i64(negative, mask) != 0) { failures++; }
            byte++
        }
        prefix++
    }
    if (!is_lower("a") || !is_upper('Z') || !is_xdigit('f') || is_xdigit('G')) { failures++; }
    if (failures != 0) { printf("ASCII extension failures: %d\n", failures); return 1; }
    return 0
}
