$include <stdio.hsh>
$include <stdlib.hsh>
$include <string.hsh>

// Separate RHS captures retain the source template, width, and interpretation.
func identity_f16(value as f16) -> f16 { return value; }
func identity_f32(value as f32) -> f32 { return value; }
func identity_f64(value as f64) -> f64 { return value; }
func identity_f128(value as f128) -> f128 { return value; }
func identity_u8(value as u8) -> u8 { return value; }
func identity_bytes(value[4] as bytes) -> (result[4] as bytes) { return value; }
func identity_cstr(value[4] as cstr) -> (result[4] as cstr) { return value; }

func main() -> i32 {
    new failures as i32 = 0
    new source16 as f16 = 2.5
    new source32 as f32 = 3.5
    new source64 as f64 = 4.5
    new source128 as f128 = 5.5
    new actual16 as f16
    new actual32 as f32
    new actual64 as f64
    new actual128 as f128
    actual16, actual32, actual64, actual128 = identity_f16(source16), identity_f32(source32), identity_f64(source64), identity_f128(source128)
    if (actual16 != source16 || actual32 != source32 || actual64 != source64 || actual128 != source128) { failures++; }
    // Mixed precision is a numeric assignment, not a raw capture copy.
    actual32, actual64 = identity_f64(source64), identity_f32(source32)
    if (actual32 != to_f32(source64) || actual64 != to_f64(source32)) { failures++; }
    actual16, actual128 = source16, source128
    if (actual16 != source16 || actual128 != source128) { failures++; }

    // Widthless explicit floating operator trees inherit the same assignment
    // context as a single RHS, including nested trees and literal operands.
    actual16, actual32 = source16 %f+ 1.0, source32 %f+ source32 %f* 2.0
    if (actual16 != to_f16(3.5) || actual32 != to_f32(10.5)) { failures++; }
    (actual32 %f=), actual128 = source32 %f+ source32 %f* 2.0, 1.5 %f+ 2.5
    if (actual32 != to_f32(10.5) || actual128 != to_f128(4.0)) { failures++; }

    new wide_truth[8]
    new truth as bool
    new zero16 as f16 = 0.0
    (wide_truth %b=), truth = identity_f128(source128), identity_f16(zero16)
    if (wide_truth != 1 || truth) { failures++; }
    new noncanonical as u8 = 128
    truth, (wide_truth %b=) = noncanonical, 0
    if (!truth || (truth as none) != 1 || wide_truth != 0) { failures++; }

    new unsigned_byte as u8 = 255
    new signed_byte as i8 = -1
    new unsigned_wide as u64
    new signed_wide as i64
    unsigned_wide, signed_wide = identity_u8(unsigned_byte), signed_byte
    if (unsigned_wide != 255 || signed_wide != -1) { failures++; }
    new raw_wide[8]
    unsigned_wide, (raw_wide %d=) = unsigned_byte, unsigned_byte?
    if (unsigned_wide != 255 || raw_wide != 255) { failures++; }
    new large_signed as i64 = -257
    new large_unsigned as u64 = 511
    signed_byte, unsigned_byte = large_signed, large_unsigned
    if (signed_byte != -1 || unsigned_byte != 255) { failures++; }

    new original[4] as bytes
    new copied[4] as bytes
    new returned[4] as bytes
    memset(original, 173, 4)
    copied, returned = original, identity_bytes(original)
    if (memcmp(original, copied, 4) != 0 || memcmp(original, returned, 4) != 0) { failures++; }
    // Bytes assignment retains the raw floating representation.
    copied, returned = source32, identity_f32(source32)
    if (memcmp(copied, source32, 4) != 0 || memcmp(returned, source32, 4) != 0) { failures++; }

    // Dynamic source lengths are captured before a target changes the length.
    memset(original, 0, 4)
    original[1] = 1
    new size as u64 = 4
    size, truth = 1, resize_bytes(original, size)
    if (size != 1 || !truth) { failures++; }
    size = 4
    size, copied = 1, resize_bytes(original, size)
    if (size != 1 || memcmp(copied, original, 4) != 0) { failures++; }

    new text1[4] as cstr = "one"
    new text2[4] as cstr = "two"
    text1, text2 = text2, text1
    if (strcmp(text1, "two") != 0 || strcmp(text2, "two") != 0) { failures++; }
    text1 = "one"
    text1, text2 = identity_cstr(text2), identity_cstr(text1)
    if (strcmp(text1, "two") != 0 || strcmp(text2, "one") != 0) { failures++; }
    new short_text[2] as cstr
    (short_text %s=), text1 = identity_cstr(text2), identity_cstr(text1)
    if (strcmp(short_text, "o") != 0 || strcmp(text1, "two") != 0) { failures++; }

    printf("multiple-rhs contracts %d\n", failures)
    return failures
}
