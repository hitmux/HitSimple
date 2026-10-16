$include <stdio.hsh>
$include <stdlib.hsh>
$include <string.hsh>

new calls as i32 = 0
new assignments as i32 = 0

template Box { value[4] as i32; }
impl Box {
    op = (dst as Box, src as Box) -> Box {
        assignments++
        dst.value = src.value + 10
        src.value = 99
        return dst
    }
}
template Pair { first[4] as i32; second[4] as i32; }
func floats() -> (first as f64, second as f64) {
    calls++
    return 0.0, 2.5
}
func integers() -> (first as i8, second as u8) {
    calls++
    new byte as u8 = 255
    return -1, byte
}
func boxes() -> (first as Box, second as Box) {
    calls++
    new box as Box
    box.value = 7
    return box, box
}
func raw() -> (first[4] as bytes, second[4] as bytes) {
    memset(first, 3, 4)
    memset(second, 4, 4)
    return
}
func wide_integers() -> (first as i64, second as u64) {
    return -257, 511
}
func offsets() -> (first as u8, second as u8) {
    return 1, 42
}
func handles(value as handle) -> (first as handle, second as handle) {
    return value, value
}
new address_calls as i32 = 0
func address(value as addr) -> addr { address_calls++; return value; }

func pair_f16(value as f16) -> (first as f16, second as f16) {
    return value, value
}
func pair_f32(value as f32) -> (first as f32, second as f32) {
    return value, value
}
func pair_f128(value as f128) -> (first as f128, second as f128) {
    return value, value
}

func main() -> i32 {
    new failures as i32 = 0
    new signed as i64
    new unsigned as u64
    signed, unsigned = integers()
    if (signed != -1 || unsigned != 255 || calls != 1) { failures++; }
    new small as i8
    new rawtarget[8]
    (small %d=), (rawtarget %d=) = integers()
    if (small != -1 || rawtarget != 255 || calls != 2) { failures++; }
    new tiny_signed as i8
    new tiny_unsigned as u8
    tiny_signed, tiny_unsigned = wide_integers()
    if (tiny_signed != -1 || tiny_unsigned != 255) { failures++; }
    new flag[8]
    new number as f64
    (flag %b=), (number %f=) = floats()
    if (flag != 0 || number != 2.5 || calls != 3) { failures++; }
    new truth as bool
    new narrower as f32
    narrower, truth = floats()
    if (narrower != to_f32(0.0) || !truth || calls != 4) { failures++; }
    new box as Box
    new other as Box
    // Named producer return assignment adds 10, then each destination adds 10.
    box, other = boxes()
    if (box.value != 27 || other.value != 27 || calls != 5 || assignments != 4) { failures++; }
    // Repeated destinations observe left-to-right writes after one capture.
    box, box = boxes()
    if (box.value != 27 || calls != 6 || assignments != 8) { failures++; }
    // Discard skips destination stores but retains producer return assignments.
    _, _ = boxes()
    if (calls != 7 || assignments != 10) { failures++; }
    _, signed = integers()
    if (signed != 255 || calls != 8) { failures++; }
    new pair as Pair
    pair.first, pair.second = integers()
    if (pair.first != -1 || pair.second != 255 || calls != 9) { failures++; }
    new buffer[8]
    buffer[0:4], buffer[4:8] = integers()
    if (buffer[0:4] != -1 || buffer[4:8] != 255 || calls != 10) { failures++; }
    new first as i32
    new second as i32
    [4]*address(&first), [4]*address(&second) = integers()
    if (first != -1 || second != 255 || calls != 11 || address_calls != 2) { failures++; }
    new offset as u8 = 0
    memset(buffer, 0, 8)
    offset, buffer[offset] = offsets()
    if (offset != 1 || buffer[0] != 0 || buffer[1] != 42) { failures++; }
    new file as handle = fopen("multi-return.bin", "w+b")
    new open as bool
    new opaque as addr
    open, (opaque as bytes) = handles(file)
    if (!open || memcmp(opaque as bytes, file as bytes, length(file as bytes)) != 0) { failures++; }
    new opaque_truth[8]
    (opaque_truth %b=), _ = handles(file)
    if (opaque_truth != 1) { failures++; }
    new closed as i32 = fclose(file)
    if (closed != 0) { failures++; }
    new bytes1[4] as bytes
    new bytes2[4] as bytes
    bytes1, bytes2 = raw()
    if (bytes1[0]? != 3 || bytes2[3]? != 4) { failures++; }
    new expected_f16 as f16 = 2.5
    new actual_f16 as f16
    new flag_f16[8]
    (flag_f16 %b=), (actual_f16 %f=) = pair_f16(expected_f16)
    if (flag_f16 != 1 || actual_f16 != expected_f16) { failures++; }
    new expected_f32 as f32 = 2.5
    new actual_f32 as f32
    new flag_f32[8]
    (flag_f32 %b=), (actual_f32 %f=) = pair_f32(expected_f32)
    if (flag_f32 != 1 || actual_f32 != expected_f32) { failures++; }
    new expected_f128 as f128 = 2.5
    new actual_f128 as f128
    new flag_f128[8]
    (flag_f128 %b=), (actual_f128 %f=) = pair_f128(expected_f128)
    if (flag_f128 != 1 || actual_f128 != expected_f128) { failures++; }
    printf("multi-return %d\n", failures)
    return failures
}
