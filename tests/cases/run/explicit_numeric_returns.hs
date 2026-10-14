$include <stdio.hsh>
$include <stdlib.hsh>

// Standard 7.3, 11.10, 12.2, and 14.2: a source-level numeric conversion
// is evaluated before the result is assigned to its return View. In particular,
// an explicit cast must not be mistaken for a return-assignment width cast.
// All float-to-integer inputs below are finite, integral, and in range.
func bool_i8(value as bool) -> i8 { return to_i8(value); }
func bool_i16(value as bool) -> i16 { return to_i16(value); }
func bool_i32(value as bool) -> i32 { return to_i32(value); }
func bool_i64(value as bool) -> i64 { return to_i64(value); }
func bool_u8(value as bool) -> u8 { return to_u8(value); }
func bool_u16(value as bool) -> u16 { return to_u16(value); }
func bool_u32(value as bool) -> u32 { return to_u32(value); }
func bool_u64(value as bool) -> u64 { return to_u64(value); }
func bool_f16(value as bool) -> f16 { return to_f16(value); }
func bool_f32(value as bool) -> f32 { return to_f32(value); }
func bool_f64(value as bool) -> f64 { return to_f64(value); }
func bool_f128(value as bool) -> f128 { return to_f128(value); }
func bool_nested(value as bool) -> i64 { return to_i64(to_u16(value)); }
func bool_widen(value as bool) -> i64 { return to_i8(value); }
func bool_narrow(value as bool) -> u8 { return to_u8(to_u64(value)); }
func bool_named(value as bool) -> result as u64 { return to_u64(value); }
func bool_pair(value as bool) -> (i64, f64) { return to_i64(value), to_f64(value); }
func bool_named_pair(value as bool) -> (first as i64, second as u64) { return to_i8(value), to_u8(value); }
func signed_direct(value as i8) -> i64 { return to_i64(value); }
func signed_nested(value as i8) -> i64 { return to_i64(to_i16(value)); }
func signed_widen(value as i8) -> i64 { return to_i16(value); }
func signed_narrow(value as i64) -> i8 { return to_i8(to_i16(value)); }
func signed_float(value as i8) -> f64 { return to_f64(value); }
func unsigned_direct(value as u8) -> u64 { return to_u64(value); }
func unsigned_nested(value as u8) -> u64 { return to_u64(to_u16(value)); }
func unsigned_widen(value as u8) -> u64 { return to_u16(value); }
func unsigned_narrow(value as u64) -> u8 { return to_u8(to_u16(value)); }
func unsigned_float(value as u8) -> f64 { return to_f64(value); }
func signed_to_unsigned(value as i64) -> u64 { return to_u64(value); }
func unsigned_to_signed(value as u64) -> i64 { return to_i64(value); }
func integer_float_nested(value as i32) -> i64 { return to_i64(to_f64(value)); }
func float_i8(value as f64) -> i8 { return to_i8(value); }
func float_i16(value as f64) -> i16 { return to_i16(value); }
func float_i32(value as f64) -> i32 { return to_i32(value); }
func float_i64(value as f64) -> i64 { return to_i64(value); }
func float_u8(value as f64) -> u8 { return to_u8(value); }
func float_u16(value as f64) -> u16 { return to_u16(value); }
func float_u32(value as f64) -> u32 { return to_u32(value); }
func float_u64(value as f64) -> u64 { return to_u64(value); }
func float_signed_widen(value as f64) -> i64 { return to_i8(value); }
func float_unsigned_widen(value as f64) -> u64 { return to_u8(value); }
func float_signed_narrow(value as f64) -> i8 { return to_i8(to_i64(value)); }
func float_unsigned_narrow(value as f64) -> u8 { return to_u8(to_u64(value)); }
func float_nested(value as f32) -> i64 { return to_i64(to_f64(value)); }
func float_named_pair(value as f64) -> (first as i64, second as u64) { return to_i8(value), to_u8(value); }

func bool_byte(value as bool) -> u8 { return value as u8; }

func check_bool(raw as u8, expected as u8) -> i32 {
    new failures as i32 = 0
    // Pass the raw bool View directly: no normalizing bool assignment may hide
    // noncanonical bytes before the explicit conversion in the callee.
    if (bool_i8(raw as bool) != to_i8(expected)) { failures++; }
    if (bool_i16(raw as bool) != to_i16(expected)) { failures++; }
    if (bool_i32(raw as bool) != to_i32(expected)) { failures++; }
    if (bool_i64(raw as bool) != to_i64(expected)) { failures++; }
    if (bool_u8(raw as bool) != to_u8(expected)) { failures++; }
    if (bool_u16(raw as bool) != to_u16(expected)) { failures++; }
    if (bool_u32(raw as bool) != to_u32(expected)) { failures++; }
    if (bool_u64(raw as bool) != to_u64(expected)) { failures++; }
    if (bool_f16(raw as bool) != to_f16(expected)) { failures++; }
    if (bool_f32(raw as bool) != to_f32(expected)) { failures++; }
    if (bool_f64(raw as bool) != to_f64(expected)) { failures++; }
    if (bool_f128(raw as bool) != to_f128(expected)) { failures++; }
    if (bool_nested(raw as bool) != to_i64(expected)) { failures++; }
    if (bool_widen(raw as bool) != to_i64(expected)) { failures++; }
    if (bool_narrow(raw as bool) != expected) { failures++; }
    if (bool_named(raw as bool) != to_u64(expected)) { failures++; }
    new integer as i64
    new floating as f64
    integer, floating = bool_pair(raw as bool)
    if (integer != to_i64(expected)) { failures++; }
    if (floating != to_f64(expected)) { failures++; }
    new unsigned_integer as u64
    integer, unsigned_integer = bool_named_pair(raw as bool)
    if (integer != to_i64(expected)) { failures++; }
    if (unsigned_integer != to_u64(expected)) { failures++; }
    if (bool_byte(raw as bool) != raw) { failures++; }
    return failures
}

func check_integer(raw as u8, signed_expected as i64) -> i32 {
    new failures as i32 = 0
    new unsigned_expected as u64 = raw
    if (signed_direct(raw as i8) != signed_expected) { failures++; }
    if (signed_nested(raw as i8) != signed_expected) { failures++; }
    if (signed_widen(raw as i8) != signed_expected) { failures++; }
    if (signed_narrow(signed_expected) != (raw as i8)) { failures++; }
    if (signed_float(raw as i8) != to_f64(signed_expected)) { failures++; }
    if (unsigned_direct(raw) != unsigned_expected) { failures++; }
    if (unsigned_nested(raw) != unsigned_expected) { failures++; }
    if (unsigned_widen(raw) != unsigned_expected) { failures++; }
    if (unsigned_narrow(unsigned_expected) != raw) { failures++; }
    if (unsigned_float(raw) != to_f64(unsigned_expected)) { failures++; }
    if (signed_to_unsigned(to_i64(raw)) != unsigned_expected) { failures++; }
    if (unsigned_to_signed(unsigned_expected) != to_i64(raw)) { failures++; }
    if (integer_float_nested(to_i32(signed_expected)) != signed_expected) { failures++; }
    return failures
}

func check_signed_float(value as f64, expected as i64) -> i32 {
    new failures as i32 = 0
    if (float_i8(value) != to_i8(expected)) { failures++; }
    if (float_i16(value) != to_i16(expected)) { failures++; }
    if (float_i32(value) != to_i32(expected)) { failures++; }
    if (float_i64(value) != expected) { failures++; }
    if (float_signed_widen(value) != expected) { failures++; }
    if (float_signed_narrow(value) != to_i8(expected)) { failures++; }
    if (float_nested(to_f32(value)) != expected) { failures++; }
    return failures
}

func check_unsigned_float(value as f64, expected as u64) -> i32 {
    new failures as i32 = 0
    if (float_u8(value) != to_u8(expected)) { failures++; }
    if (float_u16(value) != to_u16(expected)) { failures++; }
    if (float_u32(value) != to_u32(expected)) { failures++; }
    if (float_u64(value) != expected) { failures++; }
    if (float_unsigned_widen(value) != expected) { failures++; }
    if (float_unsigned_narrow(value) != to_u8(expected)) { failures++; }
    return failures
}

func check_float() -> i32 {
    new failures as i32 = 0
    new negative as i64 = -128
    failures = failures + check_signed_float(to_f64(negative), negative)
    negative = -1
    failures = failures + check_signed_float(to_f64(negative), negative)
    failures = failures + check_signed_float(0.0, 0)
    failures = failures + check_signed_float(127.0, 127)
    failures = failures + check_unsigned_float(0.0, 0)
    failures = failures + check_unsigned_float(1.0, 1)
    failures = failures + check_unsigned_float(127.0, 127)
    failures = failures + check_unsigned_float(255.0, 255)
    // Wider exact values also distinguish numeric conversion from bit copying.
    negative = -32768
    if (float_i16(to_f64(negative)) != -32768) { failures++; }
    if (float_i16(32767.0) != 32767) { failures++; }
    if (float_u16(65535.0) != 65535) { failures++; }
    negative = -2147483648
    if (float_i32(to_f64(negative)) != -2147483648) { failures++; }
    if (float_i32(2147483647.0) != 2147483647) { failures++; }
    if (float_u32(4294967295.0) != 4294967295) { failures++; }
    negative = -1099511627776
    if (float_i64(to_f64(negative)) != -1099511627776) { failures++; }
    if (float_i64(1099511627776.0) != 1099511627776) { failures++; }
    if (float_u64(1099511627776.0) != 1099511627776) { failures++; }
    new integer as i64
    new unsigned_integer as u64
    integer, unsigned_integer = float_named_pair(127.0)
    if (integer != 127 || unsigned_integer != 127) { failures++; }
    return failures
}

new evaluation_count as i32 = 0
func read_integer() -> i8 {
    evaluation_count++
    return -7
}
func return_evaluated() -> i64 { return to_i64(read_integer()); }
func return_nested_evaluated() -> i64 { return to_i16(to_i8(read_integer())); }

func main() -> i32 {
    new booleans as i32 = 0
    new integers as i32 = 0
    new index as i32 = 0
    while (index < 256) {
        new raw as u8 = to_u8(index)
        new expected as u8 = 0
        if (index != 0) { expected = 1; }
        booleans = booleans + check_bool(raw, expected)
        new signed_expected as i64 = index
        if (index >= 128) { signed_expected = index - 256; }
        integers = integers + check_integer(raw, signed_expected)
        index++
    }
    new floats as i32 = check_float()
    new evaluation as i32 = 0
    if (return_evaluated() != -7 || evaluation_count != 1) { evaluation++; }
    if (return_nested_evaluated() != -7 || evaluation_count != 2) { evaluation++; }
    printf("booleans %d\nintegers %d\nfloats %d\nevaluation %d\n", booleans, integers, floats, evaluation)
    if (booleans != 0 || integers != 0 || floats != 0 || evaluation != 0) { return 1; }
    return 0
}
