$include <stdio.hsh>
$include <stdlib.hsh>
$include <string.hsh>

// Standard 3.5.3, 4.8, 6.7 and 11.7: selection copies the complete raw
// View; only the subsequent cstr destination write stops at the first NUL.
new conditions as i32 = 0
new left_calls as i32 = 0
new right_calls as i32 = 0
new order as i32 = 0
func reset() { conditions = 0; left_calls = 0; right_calls = 0; order = 0; }
func condition(value as bool) -> bool { conditions++; order = order * 10 + 1; return value; }
func left_raw() -> [4] as bytes { left_calls++; order = order * 10 + 2; return "a\0b" as bytes; }
func right_raw() -> [4] as bytes { right_calls++; order = order * 10 + 3; return "z\0w" as bytes; }
func raw_return(choose as bool) -> [4] as bytes { return (choose ? "a\0b" : "z\0w") as bytes; }
func cstr_return(choose as bool) -> [4] as cstr { return choose ? "a\0b" : "z\0w"; }
func named_return(choose as bool) -> out[4] as cstr { return choose ? "a\0b" : "z\0w"; }
func raw_character(choose as bool) -> [3] as bytes { return (choose ? 'a\0b' : 'z\0w') as bytes; }
func same_raw(value[4] as cstr) -> [4] as bytes { return value as bytes; }
func effects(choose as bool) -> i32 {
    new failures as i32 = 0
    reset()
    new selected[4] as bytes = condition(choose) ? left_raw() : right_raw()
    if (conditions != 1) { failures++; }
    if (choose) {
        if (left_calls != 1 || right_calls != 0 || order != 12 || memcmp(selected, "a\0b", 4) != 0) { failures++; }
    } else {
        if (left_calls != 0 || right_calls != 1 || order != 13 || memcmp(selected, "z\0w", 4) != 0) { failures++; }
    }
    reset()
    new extent as u64 = length(condition(choose) ? left_raw() : right_raw())
    if (extent != 4 || conditions != 1) { failures++; }
    if (choose) {
        if (left_calls != 1 || right_calls != 0 || order != 12) { failures++; }
    } else {
        if (left_calls != 0 || right_calls != 1 || order != 13) { failures++; }
    }
    reset()
    new nested[4] as bytes = condition(choose) ? (condition(false) ? right_raw() : left_raw()) : (condition(true) ? right_raw() : left_raw())
    if (conditions != 2) { failures++; }
    if (choose) {
        if (left_calls != 1 || right_calls != 0 || order != 112 || memcmp(nested, "a\0b", 4) != 0) { failures++; }
    } else {
        if (left_calls != 0 || right_calls != 1 || order != 113 || memcmp(nested, "z\0w", 4) != 0) { failures++; }
    }
    return failures
}
func main() -> i32 {
    new failures as i32 = 0
    new original[4] as bytes = 'a\0b\0'
    new padded[4] as bytes = 'a\0\0\0'
    new other[4] as bytes = 'z\0w\0'
    new other_padded[4] as bytes = 'z\0\0\0'
    new direct[4] as cstr = false ? "z\0w" : "a\0b"
    if (memcmp(direct, padded, 4) != 0) { failures++; }
    new raw[4] as bytes = true ? "a\0b" : "z\0w"
    if (memcmp(raw, original, 4) != 0) { failures++; }
    raw = false ? "a\0b" : "z\0w"
    if (memcmp(raw, other, 4) != 0) { failures++; }
    new inferred as cstr = true ? "a\0b" : "z\0w"
    if (length(inferred) != 4 || memcmp(inferred, padded, 4) != 0) { failures++; }
    if (length(true ? "a\0b" : "z\0w") != 4) { failures++; }
    new reinterpreted as u32 = (true ? "a\0b" : "z\0w") as u32
    if (memcmp(reinterpreted, original, 4) != 0) { failures++; }
    if (memcmp((false ? "z\0w" : "a\0b") as bytes, original, 4) != 0) { failures++; }
    if (memcmp(true ? 'a\0b' : 'z\0w', 'a\0b', 3) != 0) { failures++; }
    if (memcmp(raw_character(false), 'z\0w', 3) != 0) { failures++; }
    if (memcmp(raw_return(true), original, 4) != 0 || memcmp(raw_return(false), other, 4) != 0) { failures++; }
    if (memcmp(cstr_return(true), padded, 4) != 0 || memcmp(named_return(false), other_padded, 4) != 0) { failures++; }
    // Ordinary by-value arguments copy complete Views, including cstr tails.
    if (memcmp(same_raw(true ? "a\0b" : "z\0w"), original, 4) != 0) { failures++; }
    new captured[4] as bytes = (direct = false ? "a\0b" : "z\0w")
    direct = "abc"
    if (memcmp(captured, other_padded, 4) != 0) { failures++; }
    // Both lvalue branch capture and multiple-source capture must be snapshots.
    new raw_cstr[4] as bytes = "a\0b"
    raw_cstr, captured = "xyz", true ? raw_cstr as cstr : "z\0w"
    if (memcmp(captured, original, 4) != 0 || memcmp(raw_cstr, "xyz", 4) != 0) { failures++; }
    new branch_target[4] as bytes = '0000'
    new branch_other[4] as bytes = '9999'
    captured = true ? (branch_target = "a\0b") : (branch_other = "z\0w")
    if (memcmp(captured, original, 4) != 0 || memcmp(branch_other, '9999', 4) != 0) { failures++; }
    // Boolean reads inspect all bytes, including bytes after a leading NUL.
    new truth as bool = true ? "\0ab" : "\0\0\0"
    new falsity as bool = false ? "\0ab" : "\0\0\0"
    if (!truth || falsity) { failures++; }
    new selected_leading_1[1] as cstr = true ? "\0ab" : "xyz"
    if (memcmp(selected_leading_1, '\0', 1) != 0) { failures++; }
    selected_leading_1 = false ? "xyz" : "\0ab"
    if (memcmp(selected_leading_1, '\0', 1) != 0) { failures++; }
    new selected_leading_2[2] as cstr = true ? "\0ab" : "xyz"
    if (memcmp(selected_leading_2, '\0\0', 2) != 0) { failures++; }
    selected_leading_2 = false ? "xyz" : "\0ab"
    if (memcmp(selected_leading_2, '\0\0', 2) != 0) { failures++; }
    new selected_leading_4[4] as cstr = true ? "\0ab" : "xyz"
    if (memcmp(selected_leading_4, '\0\0\0\0', 4) != 0) { failures++; }
    selected_leading_4 = false ? "xyz" : "\0ab"
    if (memcmp(selected_leading_4, '\0\0\0\0', 4) != 0) { failures++; }
    new selected_leading_6[6] as cstr = true ? "\0ab" : "xyz"
    if (memcmp(selected_leading_6, '\0\0\0\0\0\0', 6) != 0) { failures++; }
    selected_leading_6 = false ? "xyz" : "\0ab"
    if (memcmp(selected_leading_6, '\0\0\0\0\0\0', 6) != 0) { failures++; }
    new selected_middle_1[1] as cstr = true ? "a\0b" : "xyz"
    if (memcmp(selected_middle_1, '\0', 1) != 0) { failures++; }
    selected_middle_1 = false ? "xyz" : "a\0b"
    if (memcmp(selected_middle_1, '\0', 1) != 0) { failures++; }
    new selected_middle_2[2] as cstr = true ? "a\0b" : "xyz"
    if (memcmp(selected_middle_2, 'a\0', 2) != 0) { failures++; }
    selected_middle_2 = false ? "xyz" : "a\0b"
    if (memcmp(selected_middle_2, 'a\0', 2) != 0) { failures++; }
    new selected_middle_4[4] as cstr = true ? "a\0b" : "xyz"
    if (memcmp(selected_middle_4, 'a\0\0\0', 4) != 0) { failures++; }
    selected_middle_4 = false ? "xyz" : "a\0b"
    if (memcmp(selected_middle_4, 'a\0\0\0', 4) != 0) { failures++; }
    new selected_middle_6[6] as cstr = true ? "a\0b" : "xyz"
    if (memcmp(selected_middle_6, 'a\0\0\0\0\0', 6) != 0) { failures++; }
    selected_middle_6 = false ? "xyz" : "a\0b"
    if (memcmp(selected_middle_6, 'a\0\0\0\0\0', 6) != 0) { failures++; }
    new selected_trailing_1[1] as cstr = true ? "ab\0" : "xyz"
    if (memcmp(selected_trailing_1, '\0', 1) != 0) { failures++; }
    selected_trailing_1 = false ? "xyz" : "ab\0"
    if (memcmp(selected_trailing_1, '\0', 1) != 0) { failures++; }
    new selected_trailing_2[2] as cstr = true ? "ab\0" : "xyz"
    if (memcmp(selected_trailing_2, 'a\0', 2) != 0) { failures++; }
    selected_trailing_2 = false ? "xyz" : "ab\0"
    if (memcmp(selected_trailing_2, 'a\0', 2) != 0) { failures++; }
    new selected_trailing_4[4] as cstr = true ? "ab\0" : "xyz"
    if (memcmp(selected_trailing_4, 'ab\0\0', 4) != 0) { failures++; }
    selected_trailing_4 = false ? "xyz" : "ab\0"
    if (memcmp(selected_trailing_4, 'ab\0\0', 4) != 0) { failures++; }
    new selected_trailing_6[6] as cstr = true ? "ab\0" : "xyz"
    if (memcmp(selected_trailing_6, 'ab\0\0\0\0', 6) != 0) { failures++; }
    selected_trailing_6 = false ? "xyz" : "ab\0"
    if (memcmp(selected_trailing_6, 'ab\0\0\0\0', 6) != 0) { failures++; }
    new selected_plain_1[1] as cstr = true ? "abc" : "xyz"
    if (memcmp(selected_plain_1, '\0', 1) != 0) { failures++; }
    selected_plain_1 = false ? "xyz" : "abc"
    if (memcmp(selected_plain_1, '\0', 1) != 0) { failures++; }
    new selected_plain_2[2] as cstr = true ? "abc" : "xyz"
    if (memcmp(selected_plain_2, 'a\0', 2) != 0) { failures++; }
    selected_plain_2 = false ? "xyz" : "abc"
    if (memcmp(selected_plain_2, 'a\0', 2) != 0) { failures++; }
    new selected_plain_4[4] as cstr = true ? "abc" : "xyz"
    if (memcmp(selected_plain_4, 'abc\0', 4) != 0) { failures++; }
    selected_plain_4 = false ? "xyz" : "abc"
    if (memcmp(selected_plain_4, 'abc\0', 4) != 0) { failures++; }
    new selected_plain_6[6] as cstr = true ? "abc" : "xyz"
    if (memcmp(selected_plain_6, 'abc\0\0\0', 6) != 0) { failures++; }
    selected_plain_6 = false ? "xyz" : "abc"
    if (memcmp(selected_plain_6, 'abc\0\0\0', 6) != 0) { failures++; }
    new empty[1] as bytes = true ? "" : ""
    if (memcmp(empty, '\0', 1) != 0) { failures++; }
    // Exercise non-integer-sized, long, and UTF-8 literal Views.
    if (memcmp(false ? "ab" : "xy", "xy", 3) != 0) { failures++; }
    if (memcmp(true ? "abcdefghijklmnopq" : "qrstuvwxyzabcdefg", "abcdefghijklmnopq", 18) != 0) { failures++; }
    if (memcmp(true ? "你" : "好", "你", 4) != 0) { failures++; }
    failures = failures + effects(true)
    failures = failures + effects(false)
    new index as i32 = 0
    while (index < 2048) {
        order = 0
        new repeated[4] as bytes = condition(true) ? "a\0b" : "z\0w"
        if (memcmp(repeated, original, 4) != 0) { failures++; }
        index++
    }
    printf("literal ternary views %d\n", failures)
    return failures
}
