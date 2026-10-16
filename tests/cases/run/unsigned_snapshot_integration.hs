$include <stdio.hsh>
$include <stdlib.hsh>
$include <string.hsh>

new conditions as i32 = 0
new left_calls as i32 = 0
new right_calls as i32 = 0
func choose(value as bool) -> bool { conditions++; return value; }
func left() -> [4] as bytes { left_calls++; return "a\0b" as bytes; }
func right() -> [4] as bytes { right_calls++; return "z\0w" as bytes; }
func named_raw(value as bool) -> result[4] as bytes {
    return (value ? "a\0b" : "z\0w")?
}
func named_cstr(value as bool) -> result[4] as cstr {
    return ((value ? "a\0b" : "z\0w")?) as cstr
}
func named_wide(value as bool) -> result[16] {
    return (value ? 18446744073709551615 : 9223372036854775808)?
}
func named_negative() -> result[16] { return (-1)?; }
func select_live(pointer as addr, value as bool) -> [4] as bytes {
    // Forming the unselected rebound branch must not read its invalid tail.
    new rebound[4] as cstr
    rebound &= pointer
    return ((value ? rebound : "a\0b")?) as bytes
}
func bounded_cstr(pointer as addr) -> result[1] as cstr {
    new rebound[4] as cstr
    rebound &= pointer
    return rebound
}
func main() -> i32 {
    new failures as i32 = 0
    new original[4] as bytes = "a\0b"
    new other[4] as bytes = "z\0w"
    new zero[1] as bytes = '\0'
    if (memcmp(named_raw(true), original, 4) != 0 || memcmp(named_raw(false), other, 4) != 0) { failures++; }
    if (memcmp(named_cstr(true), 'a\0\0\0', 4) != 0 || memcmp(named_cstr(false), 'z\0\0\0', 4) != 0) { failures++; }
    if (memcmp(select_live(&zero, false), original, 4) != 0 || memcmp(bounded_cstr(&zero), zero, 1) != 0) { failures++; }
    new selected[4] as bytes = (choose(true) ? left() : right())?
    if (conditions != 1 || left_calls != 1 || right_calls != 0 || memcmp(selected, original, 4) != 0) { failures++; }
    selected = (choose(false) ? left() : right())?
    if (conditions != 2 || left_calls != 1 || right_calls != 1 || memcmp(selected, other, 4) != 0) { failures++; }
    if (memcmp((true ? 'é中' : 'abcde')?, 'é中', 5) != 0) { failures++; }
    if (memcmp((false ? "abcdefghijklmnopq" : "qrstuvwxyzabcdefg")?, "qrstuvwxyzabcdefg", 18) != 0) { failures++; }
    if (length((true ? "" : "")?) != 1 || (true ? "" : "")?) { failures++; }
    new expected_maximum[16] = 18446744073709551615
    new expected_upper[16] = 9223372036854775808
    if (memcmp(named_wide(true)?, expected_maximum, 16) != 0 || memcmp(named_wide(false)?, expected_upper, 16) != 0) { failures++; }
    new expected_unsigned[16] = 255
    if (memcmp(named_negative()?, expected_unsigned, 16) != 0) { failures++; }
    new wide_negative[16] = -1
    new wider[32] = wide_negative?
    new expected_wider[32] = 0
    new copied = memcpy(expected_wider, wide_negative, 16)
    if (memcmp(wider?, expected_wider, 32) != 0) { failures++; }
    new rebound[16] as bytes
    rebound &= &expected_maximum
    new saved[16] as bytes
    expected_maximum, saved = 0, (true ? rebound : expected_upper as bytes)?
    if (memcmp(saved, named_wide(true), 16) != 0 || expected_maximum?) { failures++; }
    (rebound &=), saved = &expected_upper, (true ? rebound : saved)?
    if (saved? || memcmp(rebound?, expected_upper, 16) != 0) { failures++; }
    new extent as u64 = 16
    new i as i32 = 0
    for (; i < 3; i++) {
        extent = 16
        if (i == 1) { extent = 0; }
        if (length(resize_bytes((true ? rebound : saved)?, extent)?) != extent) { failures++; }
        if (memcmp(resize_bytes((true ? rebound : saved)?, extent)?, expected_upper, extent) != 0) { failures++; }
    }
    extent = 4
    if (memcmp((resize_bytes((true ? "a\0b" : "z\0w")?, extent)?) as u32, original, 4) != 0) { failures++; }
    extent = 0
    if (length(byte_swap(resize_bytes((false ? original : other)?, extent)?)?) != 0) { failures++; }
    printf("unsigned integration %d\n", failures)
    return failures
}
