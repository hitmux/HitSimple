$include <stdio.hsh>
$include <stdlib.hsh>
$include <string.hsh>

// Expected order is built with independent byte indexing, never byte_swap.
new source_calls as u64 = 0
func source() -> f64 { source_calls++; return 0x8000000000000000 as f64; }
func returned() -> result[8] as bytes { return byte_swap(source()) as bytes; }
template Record { data[3] as bytes; }

func check1(original[1] as bytes, swapped[1] as bytes) -> i32 {
    new expected[1] as bytes
    new i as u64 = 0
    while (i < 1) { expected[i] = original[1 - i - 1]; i++; }
    if (length(swapped) != 1 || memcmp(swapped, expected, 1) != 0) { return 1; }
    return 0
}

func check2(original[2] as bytes, swapped[2] as bytes) -> i32 {
    new expected[2] as bytes
    new i as u64 = 0
    while (i < 2) { expected[i] = original[2 - i - 1]; i++; }
    if (length(swapped) != 2 || memcmp(swapped, expected, 2) != 0) { return 1; }
    return 0
}

func check3(original[3] as bytes, swapped[3] as bytes) -> i32 {
    new expected[3] as bytes
    new i as u64 = 0
    while (i < 3) { expected[i] = original[3 - i - 1]; i++; }
    if (length(swapped) != 3 || memcmp(swapped, expected, 3) != 0) { return 1; }
    return 0
}

func check4(original[4] as bytes, swapped[4] as bytes) -> i32 {
    new expected[4] as bytes
    new i as u64 = 0
    while (i < 4) { expected[i] = original[4 - i - 1]; i++; }
    if (length(swapped) != 4 || memcmp(swapped, expected, 4) != 0) { return 1; }
    return 0
}

func check5(original[5] as bytes, swapped[5] as bytes) -> i32 {
    new expected[5] as bytes
    new i as u64 = 0
    while (i < 5) { expected[i] = original[5 - i - 1]; i++; }
    if (length(swapped) != 5 || memcmp(swapped, expected, 5) != 0) { return 1; }
    return 0
}

func check8(original[8] as bytes, swapped[8] as bytes) -> i32 {
    new expected[8] as bytes
    new i as u64 = 0
    while (i < 8) { expected[i] = original[8 - i - 1]; i++; }
    if (length(swapped) != 8 || memcmp(swapped, expected, 8) != 0) { return 1; }
    return 0
}

func check16(original[16] as bytes, swapped[16] as bytes) -> i32 {
    new expected[16] as bytes
    new i as u64 = 0
    while (i < 16) { expected[i] = original[16 - i - 1]; i++; }
    if (length(swapped) != 16 || memcmp(swapped, expected, 16) != 0) { return 1; }
    return 0
}

func check33(original[33] as bytes, swapped[33] as bytes) -> i32 {
    new expected[33] as bytes
    new i as u64 = 0
    while (i < 33) { expected[i] = original[33 - i - 1]; i++; }
    if (length(swapped) != 33 || memcmp(swapped, expected, 33) != 0) { return 1; }
    return 0
}

func main() -> i32 {
    new failures as i32 = 0
    new flag as bool = true
    new integer as u16 = 0x1234
    new byte as u8 = 0x80
    new signed as i32 = -123456
    new unsigned as u64 = 0xfedcba9876543210
    new half_bits as u16 = 0x8000
    new half as f16 = half_bits as f16
    new single as f32 = 0x7fc01234 as f32
    new real as f64 = 0x8000000000000000 as f64
    new wide_bits[16] as bytes
    memset(wide_bits, 0, 16)
    wide_bits[0] %d= 0x42
    wide_bits[13] %d= 0x80
    wide_bits[14] %d= 0xff
    wide_bits[15] %d= 0x7f
    new wide as f128 = wide_bits as f128
    new raw[3] as bytes = 'ABC'
    new text[5] as cstr = "abcd"
    new record as Record
    record.data = raw
    new big[33] as bytes
    new index as u64 = 0
    while (index < 33) { big[index] %d= index + 1; index++; }
    failures = failures + check1(flag as bytes, byte_swap(flag) as bytes)
    failures = failures + check2(integer as bytes, byte_swap(integer) as bytes)
    failures = failures + check1(byte as bytes, byte_swap(byte) as bytes)
    failures = failures + check1(byte as bytes, byte_swap(byte as bool) as bytes)
    failures = failures + check4(signed as bytes, byte_swap(signed) as bytes)
    failures = failures + check8(unsigned as bytes, byte_swap(unsigned) as bytes)
    failures = failures + check2(half as bytes, byte_swap(half) as bytes)
    failures = failures + check4(single as bytes, byte_swap(single) as bytes)
    failures = failures + check8(real as bytes, byte_swap(real) as bytes)
    failures = failures + check16(wide as bytes, byte_swap(wide) as bytes)
    failures = failures + check3(raw as bytes, byte_swap(raw) as bytes)
    failures = failures + check5(text as bytes, byte_swap(text) as bytes)
    failures = failures + check3(record as bytes, byte_swap(record) as bytes)
    failures = failures + check33(big as bytes, byte_swap(big) as bytes)

    // Complete literal Views include embedded NULs and all UTF-8 bytes.
    failures = failures + check4("A\0Z" as bytes, byte_swap("A\0Z") as bytes)
    failures = failures + check5('é中' as bytes, byte_swap('é中') as bytes)
    failures = failures + check2(0x1234 as bytes, byte_swap(0x1234) as bytes)
    failures = failures + check3(big[2:5] as bytes, byte_swap(big[2:5]) as bytes)
    new address as addr = &raw
    failures = failures + check3(([3]*address) as bytes, byte_swap([3]*address) as bytes)
    if (memcmp(byte_swap(byte_swap(big)), big, 33) != 0) { failures++; }

    // Address and opaque file-handle object representations are ordinary bytes.
    new expected_pointer[P] as bytes
    index = 0
    while (index < sizeof(addr)) { expected_pointer[index] = (address as bytes)[sizeof(addr) - index - 1]; index++; }
    if (memcmp(byte_swap(address), expected_pointer, sizeof(addr)) != 0) { failures++; }
    new file as handle = fopen("byte-swap-handle.bin", "w+b")
    if (!file) { return 99; }
    index = 0
    while (index < sizeof(addr)) { expected_pointer[index] = (file as bytes)[sizeof(addr) - index - 1]; index++; }
    if (memcmp(byte_swap(file), expected_pointer, sizeof(addr)) != 0) { failures++; }
    if (fclose(file) != 0) { failures++; }

    // Read counters only after the full expression containing the call.
    new saved[8] as bytes = byte_swap(source()) as bytes
    if (source_calls != 1) { failures++; }
    failures = failures + check8(real as bytes, saved)
    saved = byte_swap(source()) as bytes
    if (source_calls != 2) { failures++; }
    failures = failures + check8(real as bytes, saved)
    saved = returned()
    if (source_calls != 3) { failures++; }
    failures = failures + check8(real as bytes, saved)
    new empty_length as u64 = length(byte_swap(resize_bytes(source(), 0)))
    if (empty_length != 0 || source_calls != 4) { failures++; }

    // The same runtime site is exercised with positive -> zero -> positive.
    new extent as u64 = 0
    new expected[33] as bytes
    new iteration as u64 = 0
    while (iteration < 6) {
        extent = 33
        if (iteration == 1 || iteration == 4) { extent = 0; }
        if (iteration == 2 || iteration == 5) { extent = 5; }
        index = 0
        while (index < extent) { expected[index] = big[extent - index - 1]; index++; }
        if (length(byte_swap(resize_bytes(big, extent))) != extent) { failures++; }
        if (memcmp(byte_swap(resize_bytes(big, extent)), expected, extent) != 0) { failures++; }
        iteration++
    }
    printf("byte swap raw views %d\n", failures)
    return failures
}
