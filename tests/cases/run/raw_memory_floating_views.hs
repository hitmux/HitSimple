$include <stdlib.hsh>
$include <string.hsh>
$include <stdio.hsh>

// Standard 14.0/14.1: raw View parameters preserve representation.
// Quiet NaNs are copied only; no arithmetic payload propagation is assumed.
new calls as i32 = 0

func identity_f16(value as f16) -> f16 { calls++; return value; }
func check_f16(value as f16) -> i32 {
    new failures as i32 = 0
    new target as f16
    new copied as addr = memcpy(target, value, 2)
    if (copied != &target || memcmp(target, value, 2) != 0) { failures++; }
    copied = memmove(target, value, 2)
    if (copied != &target || memcmp(target, value, 2) != 0) { failures++; }
    calls = 0
    copied = memcpy(target, identity_f16(value), 2)
    if (calls != 1 || memcmp(target, value, 2) != 0) { failures++; }
    calls = 0
    if (memcmp(true ? identity_f16(value) : value, value, 2) != 0 || calls != 1) { failures++; }
    memset(target, value, 2)
    new index as u64 = 0
    while (index < 2) {
        if (memcmp(target[index], value[0], 1) != 0) { failures++; }
        index++
    }
    calls = 0
    copied = memset(target, identity_f16(value), 2)
    if (calls != 1 || copied != &target) { failures++; }
    index = 0
    while (index < 2) {
        if (memcmp(target[index], value[0], 1) != 0) { failures++; }
        index++
    }
    // Borrowed destinations write caller storage; ordinary parameters stay copies.
    new zero[2] as bytes
    memset(zero, 0, 2)
    memset(target, 0, 2)
    if (memcmp(target, zero, 2) != 0) { failures++; }
    new file as handle = fopen("raw-memory-16.bin", "w+b")
    if (!file) { return 1; }
    new written as u64 = fwrite(value, 1, 2, file)
    new seeked as i32 = fseek(file, 0, 0)
    new read as u64 = fread(target, 1, 2, file)
    new closed as i32 = fclose(file)
    if (written != 2 || read != 2 || seeked != 0 || closed != 0 || memcmp(target, value, 2) != 0) { failures++; }
    return failures
}

func identity_f32(value as f32) -> f32 { calls++; return value; }
func check_f32(value as f32) -> i32 {
    new failures as i32 = 0
    new target as f32
    new copied as addr = memcpy(target, value, 4)
    if (copied != &target || memcmp(target, value, 4) != 0) { failures++; }
    copied = memmove(target, value, 4)
    if (copied != &target || memcmp(target, value, 4) != 0) { failures++; }
    calls = 0
    copied = memcpy(target, identity_f32(value), 4)
    if (calls != 1 || memcmp(target, value, 4) != 0) { failures++; }
    calls = 0
    if (memcmp(true ? identity_f32(value) : value, value, 4) != 0 || calls != 1) { failures++; }
    memset(target, value, 4)
    new index as u64 = 0
    while (index < 4) {
        if (memcmp(target[index], value[0], 1) != 0) { failures++; }
        index++
    }
    calls = 0
    copied = memset(target, identity_f32(value), 4)
    if (calls != 1 || copied != &target) { failures++; }
    index = 0
    while (index < 4) {
        if (memcmp(target[index], value[0], 1) != 0) { failures++; }
        index++
    }
    // Borrowed destinations write caller storage; ordinary parameters stay copies.
    new zero[4] as bytes
    memset(zero, 0, 4)
    memset(target, 0, 4)
    if (memcmp(target, zero, 4) != 0) { failures++; }
    new file as handle = fopen("raw-memory-32.bin", "w+b")
    if (!file) { return 1; }
    new written as u64 = fwrite(value, 1, 4, file)
    new seeked as i32 = fseek(file, 0, 0)
    new read as u64 = fread(target, 1, 4, file)
    new closed as i32 = fclose(file)
    if (written != 4 || read != 4 || seeked != 0 || closed != 0 || memcmp(target, value, 4) != 0) { failures++; }
    return failures
}

func identity_f64(value as f64) -> f64 { calls++; return value; }
func check_f64(value as f64) -> i32 {
    new failures as i32 = 0
    new target as f64
    new copied as addr = memcpy(target, value, 8)
    if (copied != &target || memcmp(target, value, 8) != 0) { failures++; }
    copied = memmove(target, value, 8)
    if (copied != &target || memcmp(target, value, 8) != 0) { failures++; }
    calls = 0
    copied = memcpy(target, identity_f64(value), 8)
    if (calls != 1 || memcmp(target, value, 8) != 0) { failures++; }
    calls = 0
    if (memcmp(true ? identity_f64(value) : value, value, 8) != 0 || calls != 1) { failures++; }
    memset(target, value, 8)
    new index as u64 = 0
    while (index < 8) {
        if (memcmp(target[index], value[0], 1) != 0) { failures++; }
        index++
    }
    calls = 0
    copied = memset(target, identity_f64(value), 8)
    if (calls != 1 || copied != &target) { failures++; }
    index = 0
    while (index < 8) {
        if (memcmp(target[index], value[0], 1) != 0) { failures++; }
        index++
    }
    // Borrowed destinations write caller storage; ordinary parameters stay copies.
    new zero[8] as bytes
    memset(zero, 0, 8)
    memset(target, 0, 8)
    if (memcmp(target, zero, 8) != 0) { failures++; }
    new file as handle = fopen("raw-memory-64.bin", "w+b")
    if (!file) { return 1; }
    new written as u64 = fwrite(value, 1, 8, file)
    new seeked as i32 = fseek(file, 0, 0)
    new read as u64 = fread(target, 1, 8, file)
    new closed as i32 = fclose(file)
    if (written != 8 || read != 8 || seeked != 0 || closed != 0 || memcmp(target, value, 8) != 0) { failures++; }
    return failures
}

func identity_f128(value as f128) -> f128 { calls++; return value; }
func check_f128(value as f128) -> i32 {
    new failures as i32 = 0
    new target as f128
    new copied as addr = memcpy(target, value, 16)
    if (copied != &target || memcmp(target, value, 16) != 0) { failures++; }
    copied = memmove(target, value, 16)
    if (copied != &target || memcmp(target, value, 16) != 0) { failures++; }
    calls = 0
    copied = memcpy(target, identity_f128(value), 16)
    if (calls != 1 || memcmp(target, value, 16) != 0) { failures++; }
    calls = 0
    if (memcmp(true ? identity_f128(value) : value, value, 16) != 0 || calls != 1) { failures++; }
    memset(target, value, 16)
    new index as u64 = 0
    while (index < 16) {
        if (memcmp(target[index], value[0], 1) != 0) { failures++; }
        index++
    }
    calls = 0
    copied = memset(target, identity_f128(value), 16)
    if (calls != 1 || copied != &target) { failures++; }
    index = 0
    while (index < 16) {
        if (memcmp(target[index], value[0], 1) != 0) { failures++; }
        index++
    }
    // Borrowed destinations write caller storage; ordinary parameters stay copies.
    new zero[16] as bytes
    memset(zero, 0, 16)
    memset(target, 0, 16)
    if (memcmp(target, zero, 16) != 0) { failures++; }
    new file as handle = fopen("raw-memory-128.bin", "w+b")
    if (!file) { return 1; }
    new written as u64 = fwrite(value, 1, 16, file)
    new seeked as i32 = fseek(file, 0, 0)
    new read as u64 = fread(target, 1, 16, file)
    new closed as i32 = fclose(file)
    if (written != 16 || read != 16 || seeked != 0 || closed != 0 || memcmp(target, value, 16) != 0) { failures++; }
    return failures
}

func dynamic(size as u64) -> i32 {
    return memcmp(resize_bytes(1, size), resize_bytes(1, size), size)
}

func main() -> i32 {
    new failures as i32 = 0
    new bits16 as u16
    bits16 = 0x0000
    failures = failures + check_f16(bits16 as f16)
    bits16 = 0x8000
    failures = failures + check_f16(bits16 as f16)
    bits16 = 0x3c71
    failures = failures + check_f16(bits16 as f16)
    bits16 = 0x7e71
    failures = failures + check_f16(bits16 as f16)
    new bits32 as u32
    bits32 = 0x00000000
    failures = failures + check_f32(bits32 as f32)
    bits32 = 0x80000000
    failures = failures + check_f32(bits32 as f32)
    bits32 = 0x3f800071
    failures = failures + check_f32(bits32 as f32)
    bits32 = 0x7fc00071
    failures = failures + check_f32(bits32 as f32)
    new bits64 as u64
    bits64 = 0x0000000000000000
    failures = failures + check_f64(bits64 as f64)
    bits64 = 0x8000000000000000
    failures = failures + check_f64(bits64 as f64)
    bits64 = 0x3ff0000000000071
    failures = failures + check_f64(bits64 as f64)
    bits64 = 0x7ff8000000000071
    failures = failures + check_f64(bits64 as f64)
    new bits128[16] as bytes
    memset(bits128, 0, 16)
    failures = failures + check_f128(bits128 as f128)
    bits128[15] = 0x80
    failures = failures + check_f128(bits128 as f128)
    bits128[0] = 0x71
    bits128[14] = 0xff
    bits128[15] = 0x3f
    failures = failures + check_f128(bits128 as f128)
    bits128[13] = 0x80
    bits128[15] = 0x7f
    failures = failures + check_f128(bits128 as f128)
    new literal as f64 = 1.0
    new destination[8] as bytes
    new copied as addr = memcpy(destination, 1.0, 8)
    if (memcmp(destination, literal, 8) != 0) { failures++; }
    memset(destination, 1.0, 8)
    new filled as addr = memset(destination, 1.0, 8)
    if (filled != copied) { failures++; }
    new byte as u8 = 0x71
    copied = memset(destination, byte, 8)
    if (memcmp(destination[0], byte, 1) != 0) { failures++; }
    copied = memset(destination, to_u8(0x123471), 8)
    if (memcmp(destination[7], byte, 1) != 0) { failures++; }
    copied = memset(destination, 'qz', 8)
    if (memcmp(destination[0], byte, 1) != 0) { failures++; }
    copied = memset(destination, "qz", 8)
    if (memcmp(destination[7], byte, 1) != 0) { failures++; }
    new raw_bool as u8 = 0x7f
    copied = memset(destination, raw_bool as bool, 8)
    if (memcmp(destination[0], raw_bool, 1) != 0) { failures++; }
    if (dynamic(12) != 0) { failures++; }
    printf("raw-memory %d\n", failures)
    if (failures != 0) { return 1; }
    return 0
}
