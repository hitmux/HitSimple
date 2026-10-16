$include <ctype.hsh>
$include <stdlib.hsh>
$include <string.hsh>
$include <stdio.hsh>

new calls as i32 = 0

func identity_f16(value as f16) -> f16 { calls++; return value; }
func check_f16(value as f16, byte as i32) -> i32 {
    new digit as bool = byte >= 48 && byte <= 57
    new upper as bool = byte >= 65 && byte <= 90
    new lower as bool = byte >= 97 && byte <= 122
    new alpha as bool = upper || lower
    new alnum as bool = digit || alpha
    new space as bool = (byte >= 9 && byte <= 13) || byte == 32
    new expected_upper as u8
    expected_upper %d= byte
    if (lower) { expected_upper %d= byte - 32; }
    new expected_lower as u8
    expected_lower %d= byte
    if (upper) { expected_lower %d= byte + 32; }
    if ((is_digit(value) as u8) != (digit as u8)) { return 1; }
    if ((is_alpha(value) as u8) != (alpha as u8)) { return 2; }
    if ((is_alnum(value) as u8) != (alnum as u8)) { return 3; }
    if ((is_space(value) as u8) != (space as u8)) { return 4; }
    if (to_upper(value) != expected_upper) { return 5; }
    if (to_lower(value) != expected_lower) { return 6; }
    calls = 0
    if (to_upper(identity_f16(value)) != expected_upper || calls != 1) { return 7; }
    if (to_lower(true ? value : value) != expected_lower) { return 8; }
    if (to_lower(value = value) != expected_lower) { return 9; }
    new count as u64 = 1
    if (to_lower(resize_bytes(value, count)) != expected_lower) { return 10; }
    return 0
}

func identity_f32(value as f32) -> f32 { calls++; return value; }
func check_f32(value as f32, byte as i32) -> i32 {
    new digit as bool = byte >= 48 && byte <= 57
    new upper as bool = byte >= 65 && byte <= 90
    new lower as bool = byte >= 97 && byte <= 122
    new alpha as bool = upper || lower
    new alnum as bool = digit || alpha
    new space as bool = (byte >= 9 && byte <= 13) || byte == 32
    new expected_upper as u8
    expected_upper %d= byte
    if (lower) { expected_upper %d= byte - 32; }
    new expected_lower as u8
    expected_lower %d= byte
    if (upper) { expected_lower %d= byte + 32; }
    if ((is_digit(value) as u8) != (digit as u8)) { return 1; }
    if ((is_alpha(value) as u8) != (alpha as u8)) { return 2; }
    if ((is_alnum(value) as u8) != (alnum as u8)) { return 3; }
    if ((is_space(value) as u8) != (space as u8)) { return 4; }
    if (to_upper(value) != expected_upper) { return 5; }
    if (to_lower(value) != expected_lower) { return 6; }
    calls = 0
    if (to_upper(identity_f32(value)) != expected_upper || calls != 1) { return 7; }
    if (to_lower(true ? value : value) != expected_lower) { return 8; }
    if (to_lower(value = value) != expected_lower) { return 9; }
    new count as u64 = 1
    if (to_lower(resize_bytes(value, count)) != expected_lower) { return 10; }
    return 0
}

func identity_f64(value as f64) -> f64 { calls++; return value; }
func check_f64(value as f64, byte as i32) -> i32 {
    new digit as bool = byte >= 48 && byte <= 57
    new upper as bool = byte >= 65 && byte <= 90
    new lower as bool = byte >= 97 && byte <= 122
    new alpha as bool = upper || lower
    new alnum as bool = digit || alpha
    new space as bool = (byte >= 9 && byte <= 13) || byte == 32
    new expected_upper as u8
    expected_upper %d= byte
    if (lower) { expected_upper %d= byte - 32; }
    new expected_lower as u8
    expected_lower %d= byte
    if (upper) { expected_lower %d= byte + 32; }
    if ((is_digit(value) as u8) != (digit as u8)) { return 1; }
    if ((is_alpha(value) as u8) != (alpha as u8)) { return 2; }
    if ((is_alnum(value) as u8) != (alnum as u8)) { return 3; }
    if ((is_space(value) as u8) != (space as u8)) { return 4; }
    if (to_upper(value) != expected_upper) { return 5; }
    if (to_lower(value) != expected_lower) { return 6; }
    calls = 0
    if (to_upper(identity_f64(value)) != expected_upper || calls != 1) { return 7; }
    if (to_lower(true ? value : value) != expected_lower) { return 8; }
    if (to_lower(value = value) != expected_lower) { return 9; }
    new count as u64 = 1
    if (to_lower(resize_bytes(value, count)) != expected_lower) { return 10; }
    return 0
}

func identity_f128(value as f128) -> f128 { calls++; return value; }
func check_f128(value as f128, byte as i32) -> i32 {
    new digit as bool = byte >= 48 && byte <= 57
    new upper as bool = byte >= 65 && byte <= 90
    new lower as bool = byte >= 97 && byte <= 122
    new alpha as bool = upper || lower
    new alnum as bool = digit || alpha
    new space as bool = (byte >= 9 && byte <= 13) || byte == 32
    new expected_upper as u8
    expected_upper %d= byte
    if (lower) { expected_upper %d= byte - 32; }
    new expected_lower as u8
    expected_lower %d= byte
    if (upper) { expected_lower %d= byte + 32; }
    if ((is_digit(value) as u8) != (digit as u8)) { return 1; }
    if ((is_alpha(value) as u8) != (alpha as u8)) { return 2; }
    if ((is_alnum(value) as u8) != (alnum as u8)) { return 3; }
    if ((is_space(value) as u8) != (space as u8)) { return 4; }
    if (to_upper(value) != expected_upper) { return 5; }
    if (to_lower(value) != expected_lower) { return 6; }
    calls = 0
    if (to_upper(identity_f128(value)) != expected_upper || calls != 1) { return 7; }
    if (to_lower(true ? value : value) != expected_lower) { return 8; }
    if (to_lower(value = value) != expected_lower) { return 9; }
    new count as u64 = 1
    if (to_lower(resize_bytes(value, count)) != expected_lower) { return 10; }
    return 0
}

func check_bytes_1(value[1] as bytes, byte as i32) -> i32 {
    new digit as bool = byte >= 48 && byte <= 57
    new upper as bool = byte >= 65 && byte <= 90
    new lower as bool = byte >= 97 && byte <= 122
    new alpha as bool = upper || lower
    new alnum as bool = digit || alpha
    new space as bool = (byte >= 9 && byte <= 13) || byte == 32
    new expected_upper as u8
    expected_upper %d= byte
    if (lower) { expected_upper %d= byte - 32; }
    new expected_lower as u8
    expected_lower %d= byte
    if (upper) { expected_lower %d= byte + 32; }
    if ((is_digit(value) as u8) != (digit as u8)) { return 1; }
    if ((is_alpha(value) as u8) != (alpha as u8)) { return 2; }
    if ((is_alnum(value) as u8) != (alnum as u8)) { return 3; }
    if ((is_space(value) as u8) != (space as u8)) { return 4; }
    if (to_upper(value) != expected_upper) { return 5; }
    if (to_lower(value) != expected_lower) { return 6; }
    return 0
}

func check_bytes_3(value[3] as bytes, byte as i32) -> i32 {
    new digit as bool = byte >= 48 && byte <= 57
    new upper as bool = byte >= 65 && byte <= 90
    new lower as bool = byte >= 97 && byte <= 122
    new alpha as bool = upper || lower
    new alnum as bool = digit || alpha
    new space as bool = (byte >= 9 && byte <= 13) || byte == 32
    new expected_upper as u8
    expected_upper %d= byte
    if (lower) { expected_upper %d= byte - 32; }
    new expected_lower as u8
    expected_lower %d= byte
    if (upper) { expected_lower %d= byte + 32; }
    if ((is_digit(value) as u8) != (digit as u8)) { return 1; }
    if ((is_alpha(value) as u8) != (alpha as u8)) { return 2; }
    if ((is_alnum(value) as u8) != (alnum as u8)) { return 3; }
    if ((is_space(value) as u8) != (space as u8)) { return 4; }
    if (to_upper(value) != expected_upper) { return 5; }
    if (to_lower(value) != expected_lower) { return 6; }
    return 0
}

func check_bytes_32(value[32] as bytes, byte as i32) -> i32 {
    new digit as bool = byte >= 48 && byte <= 57
    new upper as bool = byte >= 65 && byte <= 90
    new lower as bool = byte >= 97 && byte <= 122
    new alpha as bool = upper || lower
    new alnum as bool = digit || alpha
    new space as bool = (byte >= 9 && byte <= 13) || byte == 32
    new expected_upper as u8
    expected_upper %d= byte
    if (lower) { expected_upper %d= byte - 32; }
    new expected_lower as u8
    expected_lower %d= byte
    if (upper) { expected_lower %d= byte + 32; }
    if ((is_digit(value) as u8) != (digit as u8)) { return 1; }
    if ((is_alpha(value) as u8) != (alpha as u8)) { return 2; }
    if ((is_alnum(value) as u8) != (alnum as u8)) { return 3; }
    if ((is_space(value) as u8) != (space as u8)) { return 4; }
    if (to_upper(value) != expected_upper) { return 5; }
    if (to_lower(value) != expected_lower) { return 6; }
    return 0
}

func main() -> i32 {
    new failures as i32 = 0
    new raw[32] as bytes
    memset(raw, 0, 32)
    new byte as i32 = 0
    while (byte < 256) {
        raw[0] %d= byte
        new value_16 as f16
        new copied_16 as addr = memcpy(value_16, raw, 2)
        if (check_f16(value_16, byte) != 0) { failures++; }
        new value_32 as f32
        new copied_32 as addr = memcpy(value_32, raw, 4)
        if (check_f32(value_32, byte) != 0) { failures++; }
        new value_64 as f64
        new copied_64 as addr = memcpy(value_64, raw, 8)
        if (check_f64(value_64, byte) != 0) { failures++; }
        new value_128 as f128
        new copied_128 as addr = memcpy(value_128, raw, 16)
        if (check_f128(value_128, byte) != 0) { failures++; }
        new small_1[1] as bytes = raw[0:1]
        if (check_bytes_1(small_1, byte) != 0) { failures++; }
        new small_3[3] as bytes = raw[0:3]
        if (check_bytes_3(small_3, byte) != 0) { failures++; }
        if (check_bytes_32(raw, byte) != 0) { failures++; }
        byte++
    }
    if (!is_alpha("a") || to_upper('z') != 90) { failures++; }
    printf("ctype-view %d\n", failures)
    return failures
}
