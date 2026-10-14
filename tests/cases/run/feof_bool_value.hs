$include <stdio.hsh>
$include <stdlib.hsh>

extern "C" set_stdio_result(value as i32) -> ()

func bool_byte(value as bool) -> u8 {
    return value as u8
}

func check_stdio(value as i32, expected as u8) -> (predicates as i32, integers as i32) {
    new failures as i32 = 0
    new int_failures as i32 = 0
    new file as handle = fopen("feof-bool-value.tmp", "w+")
    if (!file) { return 100, 100; }
    set_stdio_result(value)
    // Observe the producer byte before any bool assignment or return normalizes.
    if (bool_byte(feof(file)) != expected) { failures++; }
    if ((feof(file) as u8) != expected) { failures++; }
    if (to_i32(feof(file)) != to_i32(expected)) { failures++; }
    if (bool_byte(feof(file) == true) != expected) { failures++; }
    if (bool_byte(feof(file) != false) != expected) { failures++; }
    // These neighboring stdio functions still return the complete signed C int.
    if (ferror(file) != value) { int_failures++; }
    if (fflush(file) != value) { int_failures++; }
    if (fclose(file) != value) { int_failures++; }
    return failures, int_failures
}

func main() -> i32 {
    new zero as i32
    new zero_int as i32
    new one as i32
    new one_int as i32
    new two as i32
    new two_int as i32
    new high as i32
    new high_int as i32
    new negative as i32
    new negative_int as i32
    zero, zero_int = check_stdio(0, 0)
    one, one_int = check_stdio(1, 1)
    two, two_int = check_stdio(2, 1)
    high, high_int = check_stdio(256, 1)
    new negative_value as i32 = -1
    negative, negative_int = check_stdio(negative_value, 1)
    printf("zero %d %d\none %d %d\ntwo %d %d\nhigh %d %d\nnegative %d %d\n", zero, zero_int, one, one_int, two, two_int, high, high_int, negative, negative_int)
    if (zero != 0 || one != 0 || two != 0 || high != 0 || negative != 0) { return 1; }
    if (zero_int != 0 || one_int != 0 || two_int != 0 || high_int != 0 || negative_int != 0) { return 2; }
    return 0
}
