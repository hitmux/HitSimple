$include <stdio.hsh>
$include <stdlib.hsh>

// Standard 11.5, 12.1, 12.2, 14.2, and Appendix 21: reading a bool value
// normalizes truth, but `as` and ordinary parameter copies preserve the byte.
// Never assign an input to bool storage before testing a value-read boundary.
func bool_byte(value as bool) -> u8 {
    return value as u8
}

func return_bool(value as bool) -> bool {
    return value
}

func return_byte(value as u8) -> bool {
    return value
}

func return_named(value as bool) -> result as bool {
    return value
}

func return_pair(left as bool, right as bool) -> (bool, bool) {
    return left, right
}

func return_named_pair(left as bool, right as bool) -> (first as bool, second as bool) {
    return left, right
}

func check_conversions(value as bool, expected as u8) -> i32 {
    new failures as i32 = 0
    if (to_i8(value) != to_i8(expected)) { failures++; }
    if (to_i16(value) != to_i16(expected)) { failures++; }
    if (to_i32(value) != to_i32(expected)) { failures++; }
    if (to_i64(value) != to_i64(expected)) { failures++; }
    if (to_u8(value) != to_u8(expected)) { failures++; }
    if (to_u16(value) != to_u16(expected)) { failures++; }
    if (to_u32(value) != to_u32(expected)) { failures++; }
    if (to_u64(value) != to_u64(expected)) { failures++; }
    if (to_f16(value) != to_f16(expected)) { failures++; }
    if (to_f32(value) != to_f32(expected)) { failures++; }
    if (to_f64(value) != to_f64(expected)) { failures++; }
    if (to_f128(value) != to_f128(expected)) { failures++; }
    return failures
}

func check_integer_controls(raw as u8, expected_signed as i32) -> i32 {
    new failures as i32 = 0
    new signed_wide as i64 = raw as i8
    new unsigned_wide as u64 = raw
    new signed_explicit as i64
    new unsigned_explicit as u64
    signed_explicit %d= raw as i8
    unsigned_explicit %d= raw
    if (signed_wide != to_i64(expected_signed)) { failures++; }
    if (unsigned_wide != to_u64(raw)) { failures++; }
    if (signed_explicit != to_i64(expected_signed)) { failures++; }
    if (unsigned_explicit != to_u64(raw)) { failures++; }
    if (to_i16(raw as i8) != to_i16(expected_signed)) { failures++; }
    if (to_i32(raw as i8) != to_i32(expected_signed)) { failures++; }
    if (to_i64(raw as i8) != to_i64(expected_signed)) { failures++; }
    if (to_f16(raw as i8) != to_f16(expected_signed)) { failures++; }
    if (to_f32(raw as i8) != to_f32(expected_signed)) { failures++; }
    if (to_f64(raw as i8) != to_f64(expected_signed)) { failures++; }
    if (to_f128(raw as i8) != to_f128(expected_signed)) { failures++; }
    if (to_u16(raw) != to_u16(to_u32(raw))) { failures++; }
    if (to_u32(raw) != to_u32(to_u32(raw))) { failures++; }
    if (to_u64(raw) != to_u64(to_u32(raw))) { failures++; }
    if (to_f16(raw) != to_f16(to_u32(raw))) { failures++; }
    if (to_f32(raw) != to_f32(to_u32(raw))) { failures++; }
    if (to_f64(raw) != to_f64(to_u32(raw))) { failures++; }
    if (to_f128(raw) != to_f128(to_u32(raw))) { failures++; }
    return failures
}

new left_reads as i32 = 0
new right_reads as i32 = 0

func read_left() -> u8 {
    left_reads++
    return 2
}

func read_right() -> u8 {
    right_reads++
    new raw as u8 = 255
    return raw
}

func check_equality_evaluation() -> i32 {
    new failures as i32 = 0
    if (bool_byte((read_left() as bool) == (read_right() as bool)) != 1) { failures++; }
    if (left_reads != 1 || right_reads != 1) { failures++; }
    if (bool_byte((read_left() as bool) != (read_right() as bool)) != 0) { failures++; }
    if (left_reads != 2 || right_reads != 2) { failures++; }
    return failures
}

// Keep the exhaustive inner loop in its own frame, including at -O0.
func check_pairs(raw as u8) -> i32 {
    new failures as i32 = 0
    new other_index as i32 = 0
    while (other_index < 256) {
        new other as u8 = to_u8(other_index)
        new same as u8 = 0
        if ((raw == 0 && other == 0) || (raw != 0 && other != 0)) { same = 1; }
        if (bool_byte((raw as bool) == (other as bool)) != same) { failures++; }
        if (bool_byte((raw as bool) != (other as bool)) != to_u8(1 - to_i32(same))) { failures++; }
        other_index++
    }
    return failures
}

func main() -> i32 {
    new copies as i32 = 0
    new equality as i32 = 0
    new conversions as i32 = 0
    new returns as i32 = 0
    new controls as i32 = 0
    new index as i32 = 0
    while (index < 256) {
        new raw as u8 = to_u8(index)
        new expected as u8 = 0
        if (raw != 0) { expected = 1; }
        if (((raw as bool) as u8) != raw) { copies++; }
        if (bool_byte(raw as bool) != raw) { copies++; }
        // Compare observed result bytes to integer oracles, never bool values.
        if (bool_byte((raw as bool) == true) != expected) { equality++; }
        if (bool_byte(true == (raw as bool)) != expected) { equality++; }
        if (bool_byte((raw as bool) != false) != expected) { equality++; }
        if (bool_byte(false != (raw as bool)) != expected) { equality++; }
        new inverse as u8 = to_u8(1 - to_i32(expected))
        if (bool_byte((raw as bool) == false) != inverse) { equality++; }
        if (bool_byte(false == (raw as bool)) != inverse) { equality++; }
        if (bool_byte((raw as bool) != true) != inverse) { equality++; }
        if (bool_byte(true != (raw as bool)) != inverse) { equality++; }
        conversions = conversions + check_conversions(raw as bool, expected)
        if (bool_byte(return_bool(raw as bool)) != expected) { returns++; }
        if (bool_byte(return_byte(raw)) != expected) { returns++; }
        if (bool_byte(return_named(raw as bool)) != expected) { returns++; }
        // Byte destinations deliberately avoid a bool assignment at the caller.
        new first[1] as bytes
        new second[1] as bytes
        first, second = return_pair(raw as bool, false)
        if ((first as u8) != expected) { returns++; }
        if ((second as u8) != 0) { returns++; }
        first, second = return_named_pair(false, raw as bool)
        if ((first as u8) != 0) { returns++; }
        if ((second as u8) != expected) { returns++; }
        new expected_signed as i32 = index
        if (index >= 128) { expected_signed = index - 256; }
        controls = controls + check_integer_controls(raw, expected_signed)
        equality = equality + check_pairs(raw)
        index++
    }
    new evaluation as i32 = check_equality_evaluation()
    printf("copies %d\nequality %d\nconversions %d\nreturns %d\ncontrols %d\nevaluation %d\n", copies, equality, conversions, returns, controls, evaluation)
    if (copies != 0 || equality != 0 || conversions != 0 || returns != 0 || controls != 0 || evaluation != 0) { return 1; }
    return 0
}
