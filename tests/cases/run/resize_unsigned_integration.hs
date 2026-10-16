$include <stdio.hsh>
$include <stdlib.hsh>
$include <string.hsh>

new source_calls as u64 = 0
new length_calls as u64 = 0
new branch_calls as u64 = 0
func floating() -> f64 { source_calls++; return 0x8000000000000000 as f64; }
func requested(value as u64) -> u64 { length_calls++; return value; }
func other() -> [8] as bytes { branch_calls++; return '12345678' as bytes; }
func returned() -> result[16] as bytes { return resize_bytes(floating()?, 16)?; }

func main() -> i32 {
    new failures as i32 = 0
    new source[8] as bytes = 'ABCDEFGH'
    new expected[16] as bytes
    memset(expected, 0, 16)
    new bits as u64 = 0x8000000000000000
    _ = memcpy(expected, bits, 8)
    if (memcmp(returned(), expected, 16) != 0 || source_calls != 1) { failures++; }

    // A known empty result still evaluates its source; no bytes are read from it.
    if (length(resize_bytes(floating(), 0)?) != 0 || source_calls != 2) { failures++; }
    if (length(byte_swap(resize_bytes(floating()?, 0))?) != 0 || source_calls != 3) { failures++; }
    if (resize_bytes(source, 0)?) { failures++; }
    if (memcmp(resize_bytes(source, 0)?, source, 0) != 0) { failures++; }

    // Dynamic empty extents keep both operand evaluations and compose with suffix/swap.
    new extent as u64 = 0
    if (length(resize_bytes(floating(), requested(extent))?) != 0 || source_calls != 4 || length_calls != 1) { failures++; }
    if (length(byte_swap(resize_bytes(source?, requested(extent)))?) != 0 || length_calls != 2) { failures++; }
    if (memcmp(byte_swap(resize_bytes(source, extent)?)?, source, 0) != 0) { failures++; }

    // Repeat the same registration sites with positive -> zero -> positive extents.
    // Expected byte order is constructed independently of resize and byte_swap.
    new reversed[8] as bytes
    new i as u64 = 0
    while (i < 6) {
        extent = 8
        if (i == 1 || i == 4) { extent = 0; }
        if (i == 3 || i == 5) { extent = 4; }
        new j as u64 = 0
        while (j < extent) { reversed[j] = source[extent - j - 1]; j++; }
        if (length(resize_bytes(source, extent)?) != extent) { failures++; }
        if (memcmp(resize_bytes(source, extent)?, source, extent) != 0) { failures++; }
        if (length(byte_swap(resize_bytes(source, extent)?)?) != extent) { failures++; }
        if (memcmp(byte_swap(resize_bytes(source, extent)?)?, reversed, extent) != 0) { failures++; }
        if (length(resize_bytes((true ? source : other())?, extent)?) != extent) { failures++; }
        if (memcmp(resize_bytes((false ? other() : source)?, extent)?, source, extent) != 0) { failures++; }
        if (memcmp(byte_swap(resize_bytes((true ? source : other())?, extent))?, reversed, extent) != 0) { failures++; }
        i++
    }
    if (branch_calls != 0) { failures++; }

    // Fixed copies compose with full raw ternaries, wide integers, and rebound prefixes.
    memset(expected, 0, 16)
    new embedded[4] as bytes = 'A\0Z\0'
    _ = memcpy(expected, embedded, 4)
    if (memcmp(resize_bytes((true ? "A\0Z" : "B\0Y")?, 16)?, expected, 16) != 0) { failures++; }
    if (memcmp(resize_bytes((false ? "abcde" : "é中")?, 4)?, "é中", 4) != 0) { failures++; }
    memset(expected, 0, 16)
    new maximum as u64 = 18446744073709551615
    _ = memcpy(expected, maximum, 8)
    if (memcmp(resize_bytes((true ? 18446744073709551615 : 9223372036854775808)?, 16)?, expected, 16) != 0) { failures++; }
    memset(expected, 0, 16)
    new negative as i8 = -1
    _ = memcpy(expected, negative, 1)
    if (memcmp(resize_bytes((-1)?, 16)?, expected, 16) != 0) { failures++; }
    new short[4] as bytes = 'WXYZ'
    new rebound[8] as bytes
    rebound &= &short
    if (memcmp(resize_bytes(rebound, 4)?, short, 4) != 0) { failures++; }
    if (length(resize_bytes(rebound, 0)?) != 0) { failures++; }
    printf("resize unsigned integration %d\n", failures)
    return failures
}
