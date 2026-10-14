$include <stdio.hsh>
$include <stdlib.hsh>
$include <string.hsh>

// Standard 11.10 accepts any View for bool assignment; Standard 12.2 assigns
// explicit return expressions to return Views. Test bits across the whole View.
func bool_byte(value as bool) -> u8 {
    return value as u8
}

func return_u32(value as u32) -> bool { return value; }
func return_u64(value as u64) -> result as bool { return value; }
func return_f16(value as f16) -> bool { return value; }
func return_f128(value as f128) -> bool { return value; }
func return_f32(value as f32) -> bool { return value; }
func return_f64(value as f64) -> bool { return value; }
func return_f16_sum(left as f16, right as f16) -> bool { return left + right; }
func return_f128_sum(left as f128, right as f128) -> bool { return left + right; }
func return_f32_sum(left as f32, right as f32) -> bool { return left + right; }
func return_f64_sum(left as f64, right as f64) -> bool { return left + right; }
func return_f32_choice(choose as bool, left as f32, right as f32) -> bool {
    // Preserve the existing raw-View ternary path using local storage.
    // Direct floating ABI parameter ternaries are a separate unsupported case.
    new left_copy as f32 = left
    new right_copy as f32 = right
    return choose ? left_copy : right_copy
}
func return_f64_choice(choose as bool, left as f64, right as f64) -> bool {
    // Preserve the existing raw-View ternary path using local storage.
    // Direct floating ABI parameter ternaries are a separate unsupported case.
    new left_copy as f64 = left
    new right_copy as f64 = right
    return choose ? left_copy : right_copy
}
func return_converted(value as i32) -> bool { return to_f64(value); }
func return_bytes(value[16] as bytes) -> bool { return value; }
func return_slice(value[16] as bytes, start as u64) -> bool { return value[start:+1]; }
func return_resized(value[16] as bytes, count as u64) -> bool { return resize_bytes(value, count); }
func return_addr(value as addr) -> bool { return value; }
func return_handle(value as handle) -> bool { return value; }
func return_empty_string() -> bool { return ""; }
func return_nonempty_string() -> bool { return "x"; }

template Payload {
    data[16] as bytes
}

func return_payload(value as Payload) -> bool { return value; }
func return_wide_pair(left as u64, right as f64) -> (first as bool, second as bool) {
    return left, right
}

func main() -> i32 {
    new integers as i32 = 0
    new floats as i32 = 0
    new views as i32 = 0
    new dynamic as i32 = 0
    new handles as i32 = 0
    new controls as i32 = 0
    new zero16 as u16 = 0
    new high16 as u16 = 0x8000
    new zero32 as u32 = 0
    new high32 as u32 = 0x80000000
    new zero64 as u64 = 0
    new high64 as u64 = 0x8000000000000000
    if (bool_byte(return_u32(zero32)) != 0) { integers++; }
    if (bool_byte(return_u32(high32)) != 1) { integers++; }
    if (bool_byte(return_u64(zero64)) != 0) { integers++; }
    if (bool_byte(return_u64(high64)) != 1) { integers++; }
    new zero128 as f128 = to_f128(zero64 as f64)
    new negative_zero128 as f128 = to_f128(high64 as f64)
    // Negative zero is numerically zero but has a nonzero sign bit.
    if (bool_byte(return_f16(zero16 as f16)) != 0) { floats++; }
    if (bool_byte(return_f16(high16 as f16)) != 1) { floats++; }
    if (bool_byte(return_f128(zero128)) != 0) { floats++; }
    if (bool_byte(return_f128(negative_zero128)) != 1) { floats++; }
    if (bool_byte(return_f16_sum(high16 as f16, high16 as f16)) != 1) { floats++; }
    if (bool_byte(return_f128_sum(negative_zero128, negative_zero128)) != 1) { floats++; }
    if (bool_byte(return_f16_sum(zero16 as f16, zero16 as f16)) != 0) { floats++; }
    if (bool_byte(return_f128_sum(zero128, zero128)) != 0) { floats++; }
    if (bool_byte(return_f32(zero32 as f32)) != 0) { floats++; }
    if (bool_byte(return_f32(high32 as f32)) != 1) { floats++; }
    if (bool_byte(return_f64(zero64 as f64)) != 0) { floats++; }
    if (bool_byte(return_f64(high64 as f64)) != 1) { floats++; }
    if (bool_byte(return_f32_sum(high32 as f32, high32 as f32)) != 1) { floats++; }
    if (bool_byte(return_f64_sum(high64 as f64, high64 as f64)) != 1) { floats++; }
    if (bool_byte(return_f32_sum(zero32 as f32, zero32 as f32)) != 0) { floats++; }
    if (bool_byte(return_f64_sum(zero64 as f64, zero64 as f64)) != 0) { floats++; }
    if (bool_byte(return_f32_choice(true, high32 as f32, zero32 as f32)) != 1) { floats++; }
    if (bool_byte(return_f32_choice(false, high32 as f32, zero32 as f32)) != 0) { floats++; }
    if (bool_byte(return_f64_choice(true, high64 as f64, zero64 as f64)) != 1) { floats++; }
    if (bool_byte(return_f64_choice(false, high64 as f64, zero64 as f64)) != 0) { floats++; }
    if (bool_byte(return_converted(0)) != 0) { floats++; }
    if (bool_byte(return_converted(1)) != 1) { floats++; }
    new raw[16] as bytes
    memset(raw, 0, 16)
    if (bool_byte(return_bytes(raw)) != 0) { views++; }
    if (bool_byte(return_payload(raw as Payload)) != 0) { views++; }
    if (bool_byte(return_resized(raw, 16)) != 0) { dynamic++; }
    raw[15] = 128
    if (bool_byte(return_bytes(raw)) != 1) { views++; }
    if (bool_byte(return_payload(raw as Payload)) != 1) { views++; }
    if (bool_byte(return_resized(raw, 1)) != 0) { dynamic++; }
    if (bool_byte(return_resized(raw, 16)) != 1) { dynamic++; }
    if (bool_byte(return_slice(raw, 0)) != 0) { dynamic++; }
    if (bool_byte(return_slice(raw, 15)) != 1) { dynamic++; }
    new nil as addr = 0
    new pointer as addr = &raw
    if (bool_byte(return_addr(nil)) != 0) { views++; }
    if (bool_byte(return_addr(pointer)) != 1) { views++; }
    if (bool_byte(return_empty_string()) != 0) { views++; }
    if (bool_byte(return_nonempty_string()) != 1) { views++; }
    new first[1] as bytes
    new second[1] as bytes
    first, second = return_wide_pair(high64, zero64 as f64)
    if ((first as u8) != 1 || (second as u8) != 0) { views++; }
    first, second = return_wide_pair(zero64, high64 as f64)
    if ((first as u8) != 0 || (second as u8) != 1) { views++; }
    // A guaranteed failed open supplies a null handle without reinterpretation.
    new missing as handle = fopen("bool-return-missing-directory/file", "r")
    if (bool_byte(return_handle(missing)) != 0) { handles++; }
    new file as handle = fopen("bool-return-handle.tmp", "w+")
    if (!file) { return 2; }
    if (bool_byte(return_handle(file)) != 1) { handles++; }
    if (fclose(file) != 0) { return 3; }
    // Observing/copying these source Views must not alter their representation.
    if (high32 != 0x80000000 || high64 != 0x8000000000000000) { controls++; }
    new tail as u8 = raw[15]
    new head as u8 = raw[0]
    if (tail != 128 || head != 0) { controls++; }
    if (pointer != &raw) { controls++; }
    printf("integers %d\nfloats %d\nviews %d\ndynamic %d\nhandles %d\ncontrols %d\n", integers, floats, views, dynamic, handles, controls)
    if (integers != 0 || floats != 0 || views != 0 || dynamic != 0 || handles != 0 || controls != 0) { return 1; }
    return 0
}
