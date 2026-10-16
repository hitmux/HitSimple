$include <stdio.hsh>
$include <string.hsh>

// Standard 6 and 7.10: form all source Views before ordered target writes.
// A captured lvalue keeps its location; only an rvalue keeps its old bytes.
// These cases do not require an order between independent RHS evaluations.
// Address rebinding (&=) has a separate contract and is not covered here.
new g as i32 = 3
new first_calls as i32 = 0
new second_calls as i32 = 0
new source_address_calls as i32 = 0
new source_index_calls as i32 = 0
new target_address_calls as i32 = 0
new index as u8 = 0
new assignments as i32 = 0
new assignment_order as i32 = 0

func observe() -> i32 { return g; }
func first_effect() -> i32 { first_calls++; return 11; }
func second_effect() -> i32 { second_calls++; return 22; }
func replace_g() -> i32 { g = 5; return 9; }
func source_address(value as addr) -> addr {
    source_address_calls++
    return value
}
func source_index() -> u8 { source_index_calls++; return index; }
func target_address(value as addr) -> addr {
    target_address_calls++
    return value
}

template Box { value[4] as i32; }
impl Box {
    op = (dst as Box, src as Box) -> Box {
        assignments++
        assignment_order = assignment_order * 10 + dst.value
        dst.value = src.value + 10
        // Only the destination is borrowed. The source parameter is a copy.
        src.value = 99
        return dst
    }
}
template Pair { first[4] as i32; second[4] as i32; }

func main() -> i32 {
    new failures as i32 = 0
    new out as i32

    // The call must run while g is still 3, before the first target write.
    g, out = 9, observe()
    if (g != 9 || out != 3) { failures++; }

    // Single, all, and partial discards still evaluate each source once.
    _ = first_effect()
    if (first_calls != 1) { failures++; }
    _, _ = first_effect(), second_effect()
    if (first_calls != 2 || second_calls != 1) { failures++; }
    _, out = first_effect(), second_effect()
    if (first_calls != 3 || second_calls != 2 || out != 22) { failures++; }
    out, _ = first_effect(), second_effect()
    if (first_calls != 4 || second_calls != 3 || out != 11) { failures++; }

    new x as i32 = 1
    new y as i32 = 2
    x, y = y, x
    if (x != 2 || y != 2) { failures++; }
    x = 1
    y = 2
    x, y = y + 0, x + 0
    if (x != 2 || y != 1) { failures++; }

    // Forming the g lvalue does not read its bytes. The later assignment
    // reads g after the first target write, even though replace_g ran earlier.
    g = 3
    g, out = replace_g(), g
    if (g != 9 || out != 9) { failures++; }
    // The compound target reads its old value only after every RHS ran.
    g = 3
    (g %d+=), out = 1, replace_g()
    if (g != 6 || out != 9) { failures++; }

    // Source pointers and indices are evaluated once before target writes.
    new first as i32 = 31
    new second as i32 = 47
    new pointer as addr = &first
    pointer, out = &second, [4]*source_address(pointer)
    if (pointer != &second || out != 31 || source_address_calls != 1) { failures++; }
    first, out = 73, [4]*source_address(&first)
    if (first != 73 || out != 73 || source_address_calls != 2) { failures++; }
    new buffer[8]
    memset(buffer, 0, 8)
    buffer[0] = 31
    buffer[1] = 47
    index, out = 1, buffer[source_index()]
    if (index != 1 || out != 31 || source_index_calls != 1) { failures++; }
    buffer[index], out = 61, buffer[source_index()]
    if (buffer[1] != 61 || out != 61 || source_index_calls != 2) { failures++; }

    // Targets are evaluated in target order, after all source evaluations.
    index = 0
    index, buffer[index] = 1, 42
    if (index != 1 || buffer[0] != 31 || buffer[1] != 42) { failures++; }
    pointer = &first
    pointer, [4]*target_address(pointer) = &second, 58
    if (first != 73 || second != 58 || target_address_calls != 1) { failures++; }
    new pair as Pair
    pair.first, pair.second = 17, 29
    if (pair.first != 17 || pair.second != 29) { failures++; }
    pair.first, pair.second = pair.second, pair.first
    if (pair.first != 29 || pair.second != 29) { failures++; }
    buffer[0:4], buffer[4:8] = 101, 202
    if (buffer[0:4] != 101 || buffer[4:8] != 202) { failures++; }
    buffer[0:4], buffer[4:8] = buffer[4:8], buffer[0:4]
    if (buffer[0:4] != 202 || buffer[4:8] != 202) { failures++; }
    x, x = 4, 5
    if (x != 5) { failures++; }

    // User assignment borrows destinations in order and copies each source
    // when that destination operation runs, preserving lvalue aliasing.
    new left as Box
    new right as Box
    left.value = 1
    right.value = 2
    left, right = right, left
    if (left.value != 12 || right.value != 22 || assignments != 2 || assignment_order != 12) { failures++; }
    new source as Box
    source.value = 7
    left.value = 3
    right.value = 4
    assignment_order = 0
    left, right = source, source
    if (left.value != 17 || right.value != 17 || source.value != 7 || assignments != 4 || assignment_order != 34) { failures++; }
    _, _ = source, source
    if (assignments != 4 || source.value != 7) { failures++; }

    printf("multiple-rhs evaluation %d\n", failures)
    return failures
}
