$include <stdio.hsh>
$include <stdlib.hsh>
$include <string.hsh>
$include <ctype.hsh>

template Blob { bytes[5] as bytes; }
new calls as u64 = 0
func count() -> u64 { calls++; return 3; }
func floating() -> f64 { calls++; return 0x8000000000000000 as f64; }
func position(value as addr) -> addr { calls++; return value; }
func check_f16(value as f16) -> i32 {
    new failures as i32 = 0
    new copied[2] as bytes = value?
    new integer[2] = value?
    if (length(value?) != 2 || memcmp(copied, value, 2) != 0 || memcmp(integer, value, 2) != 0) { failures++; }
    if (memcmp(value?, value, 2) != 0 || memcmp((value?) as bytes, value, 2) != 0) { failures++; }
    new operated[2] = value? %2d| 0
    if (memcmp(operated, value, 2) != 0) { failures++; }
    return failures
}
func check_f32(value as f32) -> i32 {
    new failures as i32 = 0
    new copied[4] as bytes = value?
    new integer[4] = value?
    if (length(value?) != 4 || memcmp(copied, value, 4) != 0 || memcmp(integer, value, 4) != 0) { failures++; }
    if (memcmp(value?, value, 4) != 0 || memcmp((value?) as bytes, value, 4) != 0) { failures++; }
    new operated[4] = value? %4d| 0
    if (memcmp(operated, value, 4) != 0) { failures++; }
    return failures
}
func check_f64(value as f64) -> i32 {
    new failures as i32 = 0
    new copied[8] as bytes = value?
    new integer[8] = value?
    if (length(value?) != 8 || memcmp(copied, value, 8) != 0 || memcmp(integer, value, 8) != 0) { failures++; }
    if (memcmp(value?, value, 8) != 0 || memcmp((value?) as bytes, value, 8) != 0) { failures++; }
    new operated[8] = value? %8d| 0
    if (memcmp(operated, value, 8) != 0) { failures++; }
    return failures
}
func check_f128(value as f128) -> i32 {
    new failures as i32 = 0
    new copied[16] as bytes = value?
    new integer[16] = value?
    if (length(value?) != 16 || memcmp(copied, value, 16) != 0 || memcmp(integer, value, 16) != 0) { failures++; }
    if (memcmp(value?, value, 16) != 0 || memcmp((value?) as bytes, value, 16) != 0) { failures++; }
    new operated[16] = value? | 0
    if (memcmp(operated, value, 16) != 0) { failures++; }
    return failures
}
func main() -> i32 {
    new failures as i32 = 0
    new bits16[2] = 0
    failures = failures + check_f16(bits16 as f16)
    bits16 = 1 %2d<< 15
    failures = failures + check_f16(bits16 as f16)
    bits16 = (31 %2d<< 10) %2d| 1
    failures = failures + check_f16(bits16 as f16)
    bits16 = bits16 %2d| (1 %2d<< 15)
    failures = failures + check_f16(bits16 as f16)
    new bits32[4] = 0
    failures = failures + check_f32(bits32 as f32)
    bits32 = 1 %4d<< 31
    failures = failures + check_f32(bits32 as f32)
    bits32 = (255 %4d<< 23) %4d| 1
    failures = failures + check_f32(bits32 as f32)
    bits32 = bits32 %4d| (1 %4d<< 31)
    failures = failures + check_f32(bits32 as f32)
    new bits64[8] = 0
    failures = failures + check_f64(bits64 as f64)
    bits64 = 1 %8d<< 63
    failures = failures + check_f64(bits64 as f64)
    bits64 = (2047 %8d<< 52) %8d| 1
    failures = failures + check_f64(bits64 as f64)
    bits64 = bits64 %8d| (1 %8d<< 63)
    failures = failures + check_f64(bits64 as f64)
    new zero as u64 = 0
    new one as u64 = 1
    new exponent as u64 = 32767
    new bits128[16] = zero
    failures = failures + check_f128(bits128 as f128)
    bits128 = one
    bits128 = bits128 << 127
    failures = failures + check_f128(bits128 as f128)
    bits128 = exponent
    bits128 = (bits128 << 112) | 1
    failures = failures + check_f128(bits128 as f128)
    new negative128[16] = one
    bits128 = bits128 | (negative128 << 127)
    failures = failures + check_f128(bits128 as f128)
    new raw[33] as bytes
    memset(raw, 0, 33)
    raw[0] = 1
    raw[16] = 87
    raw[32] = 127
    if (length(raw?) != 33 || memcmp(raw?, raw, 33) != 0) { failures++; }
    if (length('é中'?) != 5 || memcmp('é中'?, 'é中', 5) != 0) { failures++; }
    if (length("A\0B"?) != 4 || memcmp("A\0B"?, "A\0B", 4) != 0) { failures++; }
    if ('A'? != 65 || 65535? != 65535 || length(65535?) != 4) { failures++; }
    new text[5] as cstr = "test"
    if (memcmp(text?, text, 5) != 0 || length(text?) != 5) { failures++; }
    new unterminated[3] as bytes = 'ABC'
    set unterminated as cstr
    if (memcmp(unterminated?, 'ABC', 3) != 0) { failures++; }
    new user as Blob
    user.bytes = 'abcde'
    if (memcmp(user?, user, 5) != 0 || length(user?) != 5) { failures++; }
    new signed as i16 = -1
    if (signed? != 65535 || length(signed?) != 2) { failures++; }
    new unsigned as u16 = 65535
    if (unsigned? != 65535) { failures++; }
    new flag as bool = true
    flag[0] = 0x80
    if (flag? != 128 || length(flag?) != 1) { failures++; }
    new pointer as addr = &raw
    if (memcmp(pointer?, pointer as bytes, length(pointer)) != 0 || length(pointer?) != length(pointer)) { failures++; }
    new handle as handle = pointer as handle
    if (memcmp(handle?, handle as bytes, length(pointer)) != 0 || length(handle?) != length(pointer)) { failures++; }

    // All RHS Views are captured before writes: ? freezes bytes, not location.
    new source[5] as bytes = 'abcde'
    new saved[5] as bytes
    source, saved = 'vwxyz', source?
    if (memcmp(saved, 'abcde', 5) != 0 || memcmp(source, 'vwxyz', 5) != 0) { failures++; }
    new other[5] as bytes = '12345'
    (source &=), saved = &other, source?
    if (memcmp(saved, 'vwxyz', 5) != 0 || memcmp(source, '12345', 5) != 0) { failures++; }
    source, saved = '67890', source?
    if (memcmp(saved, '12345', 5) != 0 || memcmp(other, '67890', 5) != 0) { failures++; }

    calls = 0
    if (length(floating()?) != 8 || calls != 1) { failures++; }
    if (memcmp(([5]*position(&source))?, source, 5) != 0 || calls != 2) { failures++; }
    if (length(resize_bytes(raw, count())?) != 3 || calls != 3) { failures++; }
    new extent as u64 = 33
    if (length(resize_bytes(raw, extent)?) != 33 || memcmp(resize_bytes(raw, extent)?, raw, 33) != 0) { failures++; }
    if (!resize_bytes(raw, extent)?) { failures++; }
    if (memcmp(byte_swap(byte_swap(resize_bytes(raw, extent)?))?, raw, 33) != 0) { failures++; }
    extent = 0
    if (length(resize_bytes(raw, extent)?) != 0 || resize_bytes(raw, extent)?) { failures++; }
    if (length(resize_bytes(raw, 0)?) != 0 || length(byte_swap(resize_bytes(raw, 0)?)?) != 0) { failures++; }
    if (memcmp(resize_bytes(raw, extent)?, raw, 0) != 0) { failures++; }
    new dynamic_source[3] as bytes = 'ABC'
    new dynamic_flag as bool = false
    extent = 3
    dynamic_source, dynamic_flag = '\0\0\0', resize_bytes(dynamic_source, extent)?
    if (!dynamic_flag || dynamic_source?) { failures++; }
    // Revisit the same dynamic temporary with positive, empty, then positive extents.
    new i as i32 = 0
    for (; i < 3; i++) {
        extent = 3
        if (i == 1) { extent = 0; }
        if (length(resize_bytes(raw, extent)?) != extent) { failures++; }
    }
    printf("unsigned-views %d\n", failures)
    return failures
}
