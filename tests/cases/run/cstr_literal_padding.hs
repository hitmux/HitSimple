$include <stdio.hsh>
$include <stdlib.hsh>
$include <string.hsh>

new calls as i32 = 0
new global_literal[4] as cstr = "a\0b"

func named_leading_1() -> out[1] as cstr { calls++; return "\0ab"; }
func anonymous_leading_1() -> [1] as cstr { calls++; return "\0ab"; }
func explicit_leading_1() -> out[1] as bytes { calls++; out %s= "\0ab"; return; }
func named_leading_3() -> out[3] as cstr { calls++; return "\0ab"; }
func anonymous_leading_3() -> [3] as cstr { calls++; return "\0ab"; }
func explicit_leading_3() -> out[3] as bytes { calls++; out %s= "\0ab"; return; }
func named_leading_4() -> out[4] as cstr { calls++; return "\0ab"; }
func anonymous_leading_4() -> [4] as cstr { calls++; return "\0ab"; }
func explicit_leading_4() -> out[4] as bytes { calls++; out %s= "\0ab"; return; }
func named_leading_6() -> out[6] as cstr { calls++; return "\0ab"; }
func anonymous_leading_6() -> [6] as cstr { calls++; return "\0ab"; }
func explicit_leading_6() -> out[6] as bytes { calls++; out %s= "\0ab"; return; }
func named_middle_1() -> out[1] as cstr { calls++; return "a\0b"; }
func anonymous_middle_1() -> [1] as cstr { calls++; return "a\0b"; }
func explicit_middle_1() -> out[1] as bytes { calls++; out %s= "a\0b"; return; }
func named_middle_3() -> out[3] as cstr { calls++; return "a\0b"; }
func anonymous_middle_3() -> [3] as cstr { calls++; return "a\0b"; }
func explicit_middle_3() -> out[3] as bytes { calls++; out %s= "a\0b"; return; }
func named_middle_4() -> out[4] as cstr { calls++; return "a\0b"; }
func anonymous_middle_4() -> [4] as cstr { calls++; return "a\0b"; }
func explicit_middle_4() -> out[4] as bytes { calls++; out %s= "a\0b"; return; }
func named_middle_6() -> out[6] as cstr { calls++; return "a\0b"; }
func anonymous_middle_6() -> [6] as cstr { calls++; return "a\0b"; }
func explicit_middle_6() -> out[6] as bytes { calls++; out %s= "a\0b"; return; }
func named_trailing_1() -> out[1] as cstr { calls++; return "ab\0"; }
func anonymous_trailing_1() -> [1] as cstr { calls++; return "ab\0"; }
func explicit_trailing_1() -> out[1] as bytes { calls++; out %s= "ab\0"; return; }
func named_trailing_3() -> out[3] as cstr { calls++; return "ab\0"; }
func anonymous_trailing_3() -> [3] as cstr { calls++; return "ab\0"; }
func explicit_trailing_3() -> out[3] as bytes { calls++; out %s= "ab\0"; return; }
func named_trailing_4() -> out[4] as cstr { calls++; return "ab\0"; }
func anonymous_trailing_4() -> [4] as cstr { calls++; return "ab\0"; }
func explicit_trailing_4() -> out[4] as bytes { calls++; out %s= "ab\0"; return; }
func named_trailing_6() -> out[6] as cstr { calls++; return "ab\0"; }
func anonymous_trailing_6() -> [6] as cstr { calls++; return "ab\0"; }
func explicit_trailing_6() -> out[6] as bytes { calls++; out %s= "ab\0"; return; }
func named_empty_1() -> out[1] as cstr { calls++; return ""; }
func anonymous_empty_1() -> [1] as cstr { calls++; return ""; }
func explicit_empty_1() -> out[1] as bytes { calls++; out %s= ""; return; }
func named_empty_4() -> out[4] as cstr { calls++; return ""; }
func anonymous_empty_4() -> [4] as cstr { calls++; return ""; }
func explicit_empty_4() -> out[4] as bytes { calls++; out %s= ""; return; }
func named_plain_1() -> out[1] as cstr { calls++; return "abcd"; }
func anonymous_plain_1() -> [1] as cstr { calls++; return "abcd"; }
func explicit_plain_1() -> out[1] as bytes { calls++; out %s= "abcd"; return; }
func named_plain_3() -> out[3] as cstr { calls++; return "abcd"; }
func anonymous_plain_3() -> [3] as cstr { calls++; return "abcd"; }
func explicit_plain_3() -> out[3] as bytes { calls++; out %s= "abcd"; return; }
func named_plain_4() -> out[4] as cstr { calls++; return "abcd"; }
func anonymous_plain_4() -> [4] as cstr { calls++; return "abcd"; }
func explicit_plain_4() -> out[4] as bytes { calls++; out %s= "abcd"; return; }
func named_plain_6() -> out[6] as cstr { calls++; return "abcd"; }
func anonymous_plain_6() -> [6] as cstr { calls++; return "abcd"; }
func explicit_plain_6() -> out[6] as bytes { calls++; out %s= "abcd"; return; }

func cached_literal() -> [4] as bytes { static cached[4] as cstr = "a\0b"; return cached as bytes; }
template Framed { head[1] as u8; text[4] as cstr; tail[1] as u8; }

func main() -> i32 {
    new failures as i32 = 0
    // leading NUL shape, destination capacity 1; compare every byte.
    new expected_leading_1[1] as bytes = '\0'
    new declared_leading_1[1] as cstr = "\0ab"
    new explicit_leading_1[1] as bytes %s= "\0ab"
    new assigned_leading_1[1] as cstr = "zzzzzz"
    assigned_leading_1 = "\0ab"
    new reassigned_leading_1[1] as bytes %s= "zzzzzz"
    reassigned_leading_1 %s= "\0ab"
    if (memcmp(declared_leading_1, expected_leading_1, 1) != 0) { failures++; }
    if (memcmp(explicit_leading_1, expected_leading_1, 1) != 0) { failures++; }
    if (memcmp(assigned_leading_1, expected_leading_1, 1) != 0) { failures++; }
    if (memcmp(reassigned_leading_1, expected_leading_1, 1) != 0) { failures++; }
    new result_leading_1[1] as bytes = named_leading_1()
    if (memcmp(result_leading_1, expected_leading_1, 1) != 0) { failures++; }
    result_leading_1 = anonymous_leading_1()
    if (memcmp(result_leading_1, expected_leading_1, 1) != 0) { failures++; }
    result_leading_1 = explicit_leading_1()
    if (memcmp(result_leading_1, expected_leading_1, 1) != 0) { failures++; }

    // leading NUL shape, destination capacity 3; compare every byte.
    new expected_leading_3[3] as bytes = '\0\0\0'
    new declared_leading_3[3] as cstr = "\0ab"
    new explicit_leading_3[3] as bytes %s= "\0ab"
    new assigned_leading_3[3] as cstr = "zzzzzz"
    assigned_leading_3 = "\0ab"
    new reassigned_leading_3[3] as bytes %s= "zzzzzz"
    reassigned_leading_3 %s= "\0ab"
    if (memcmp(declared_leading_3, expected_leading_3, 3) != 0) { failures++; }
    if (memcmp(explicit_leading_3, expected_leading_3, 3) != 0) { failures++; }
    if (memcmp(assigned_leading_3, expected_leading_3, 3) != 0) { failures++; }
    if (memcmp(reassigned_leading_3, expected_leading_3, 3) != 0) { failures++; }
    new result_leading_3[3] as bytes = named_leading_3()
    if (memcmp(result_leading_3, expected_leading_3, 3) != 0) { failures++; }
    result_leading_3 = anonymous_leading_3()
    if (memcmp(result_leading_3, expected_leading_3, 3) != 0) { failures++; }
    result_leading_3 = explicit_leading_3()
    if (memcmp(result_leading_3, expected_leading_3, 3) != 0) { failures++; }

    // leading NUL shape, destination capacity 4; compare every byte.
    new expected_leading_4[4] as bytes = '\0\0\0\0'
    new declared_leading_4[4] as cstr = "\0ab"
    new explicit_leading_4[4] as bytes %s= "\0ab"
    new assigned_leading_4[4] as cstr = "zzzzzz"
    assigned_leading_4 = "\0ab"
    new reassigned_leading_4[4] as bytes %s= "zzzzzz"
    reassigned_leading_4 %s= "\0ab"
    if (memcmp(declared_leading_4, expected_leading_4, 4) != 0) { failures++; }
    if (memcmp(explicit_leading_4, expected_leading_4, 4) != 0) { failures++; }
    if (memcmp(assigned_leading_4, expected_leading_4, 4) != 0) { failures++; }
    if (memcmp(reassigned_leading_4, expected_leading_4, 4) != 0) { failures++; }
    new result_leading_4[4] as bytes = named_leading_4()
    if (memcmp(result_leading_4, expected_leading_4, 4) != 0) { failures++; }
    result_leading_4 = anonymous_leading_4()
    if (memcmp(result_leading_4, expected_leading_4, 4) != 0) { failures++; }
    result_leading_4 = explicit_leading_4()
    if (memcmp(result_leading_4, expected_leading_4, 4) != 0) { failures++; }

    // leading NUL shape, destination capacity 6; compare every byte.
    new expected_leading_6[6] as bytes = '\0\0\0\0\0\0'
    new declared_leading_6[6] as cstr = "\0ab"
    new explicit_leading_6[6] as bytes %s= "\0ab"
    new assigned_leading_6[6] as cstr = "zzzzzz"
    assigned_leading_6 = "\0ab"
    new reassigned_leading_6[6] as bytes %s= "zzzzzz"
    reassigned_leading_6 %s= "\0ab"
    if (memcmp(declared_leading_6, expected_leading_6, 6) != 0) { failures++; }
    if (memcmp(explicit_leading_6, expected_leading_6, 6) != 0) { failures++; }
    if (memcmp(assigned_leading_6, expected_leading_6, 6) != 0) { failures++; }
    if (memcmp(reassigned_leading_6, expected_leading_6, 6) != 0) { failures++; }
    new result_leading_6[6] as bytes = named_leading_6()
    if (memcmp(result_leading_6, expected_leading_6, 6) != 0) { failures++; }
    result_leading_6 = anonymous_leading_6()
    if (memcmp(result_leading_6, expected_leading_6, 6) != 0) { failures++; }
    result_leading_6 = explicit_leading_6()
    if (memcmp(result_leading_6, expected_leading_6, 6) != 0) { failures++; }

    // Raw literal Views retain all decoded bytes and their full extent.
    new raw_leading[4] as bytes = "\0ab"
    new raw_expected_leading[4] as bytes = '\0ab\0'
    if (memcmp(raw_leading, raw_expected_leading, 4) != 0) { failures++; }
    raw_leading = "\0ab"
    if (memcmp(raw_leading, raw_expected_leading, 4) != 0) { failures++; }
    if (memcmp("\0ab", raw_expected_leading, 4) != 0) { failures++; }
    new inferred_leading as cstr = "\0ab"
    if (length("\0ab") != 4 || length(inferred_leading) != 4) { failures++; }

    // middle NUL shape, destination capacity 1; compare every byte.
    new expected_middle_1[1] as bytes = '\0'
    new declared_middle_1[1] as cstr = "a\0b"
    new explicit_middle_1[1] as bytes %s= "a\0b"
    new assigned_middle_1[1] as cstr = "zzzzzz"
    assigned_middle_1 = "a\0b"
    new reassigned_middle_1[1] as bytes %s= "zzzzzz"
    reassigned_middle_1 %s= "a\0b"
    if (memcmp(declared_middle_1, expected_middle_1, 1) != 0) { failures++; }
    if (memcmp(explicit_middle_1, expected_middle_1, 1) != 0) { failures++; }
    if (memcmp(assigned_middle_1, expected_middle_1, 1) != 0) { failures++; }
    if (memcmp(reassigned_middle_1, expected_middle_1, 1) != 0) { failures++; }
    new result_middle_1[1] as bytes = named_middle_1()
    if (memcmp(result_middle_1, expected_middle_1, 1) != 0) { failures++; }
    result_middle_1 = anonymous_middle_1()
    if (memcmp(result_middle_1, expected_middle_1, 1) != 0) { failures++; }
    result_middle_1 = explicit_middle_1()
    if (memcmp(result_middle_1, expected_middle_1, 1) != 0) { failures++; }

    // middle NUL shape, destination capacity 3; compare every byte.
    new expected_middle_3[3] as bytes = 'a\0\0'
    new declared_middle_3[3] as cstr = "a\0b"
    new explicit_middle_3[3] as bytes %s= "a\0b"
    new assigned_middle_3[3] as cstr = "zzzzzz"
    assigned_middle_3 = "a\0b"
    new reassigned_middle_3[3] as bytes %s= "zzzzzz"
    reassigned_middle_3 %s= "a\0b"
    if (memcmp(declared_middle_3, expected_middle_3, 3) != 0) { failures++; }
    if (memcmp(explicit_middle_3, expected_middle_3, 3) != 0) { failures++; }
    if (memcmp(assigned_middle_3, expected_middle_3, 3) != 0) { failures++; }
    if (memcmp(reassigned_middle_3, expected_middle_3, 3) != 0) { failures++; }
    new result_middle_3[3] as bytes = named_middle_3()
    if (memcmp(result_middle_3, expected_middle_3, 3) != 0) { failures++; }
    result_middle_3 = anonymous_middle_3()
    if (memcmp(result_middle_3, expected_middle_3, 3) != 0) { failures++; }
    result_middle_3 = explicit_middle_3()
    if (memcmp(result_middle_3, expected_middle_3, 3) != 0) { failures++; }

    // middle NUL shape, destination capacity 4; compare every byte.
    new expected_middle_4[4] as bytes = 'a\0\0\0'
    new declared_middle_4[4] as cstr = "a\0b"
    new explicit_middle_4[4] as bytes %s= "a\0b"
    new assigned_middle_4[4] as cstr = "zzzzzz"
    assigned_middle_4 = "a\0b"
    new reassigned_middle_4[4] as bytes %s= "zzzzzz"
    reassigned_middle_4 %s= "a\0b"
    if (memcmp(declared_middle_4, expected_middle_4, 4) != 0) { failures++; }
    if (memcmp(explicit_middle_4, expected_middle_4, 4) != 0) { failures++; }
    if (memcmp(assigned_middle_4, expected_middle_4, 4) != 0) { failures++; }
    if (memcmp(reassigned_middle_4, expected_middle_4, 4) != 0) { failures++; }
    new result_middle_4[4] as bytes = named_middle_4()
    if (memcmp(result_middle_4, expected_middle_4, 4) != 0) { failures++; }
    result_middle_4 = anonymous_middle_4()
    if (memcmp(result_middle_4, expected_middle_4, 4) != 0) { failures++; }
    result_middle_4 = explicit_middle_4()
    if (memcmp(result_middle_4, expected_middle_4, 4) != 0) { failures++; }

    // middle NUL shape, destination capacity 6; compare every byte.
    new expected_middle_6[6] as bytes = 'a\0\0\0\0\0'
    new declared_middle_6[6] as cstr = "a\0b"
    new explicit_middle_6[6] as bytes %s= "a\0b"
    new assigned_middle_6[6] as cstr = "zzzzzz"
    assigned_middle_6 = "a\0b"
    new reassigned_middle_6[6] as bytes %s= "zzzzzz"
    reassigned_middle_6 %s= "a\0b"
    if (memcmp(declared_middle_6, expected_middle_6, 6) != 0) { failures++; }
    if (memcmp(explicit_middle_6, expected_middle_6, 6) != 0) { failures++; }
    if (memcmp(assigned_middle_6, expected_middle_6, 6) != 0) { failures++; }
    if (memcmp(reassigned_middle_6, expected_middle_6, 6) != 0) { failures++; }
    new result_middle_6[6] as bytes = named_middle_6()
    if (memcmp(result_middle_6, expected_middle_6, 6) != 0) { failures++; }
    result_middle_6 = anonymous_middle_6()
    if (memcmp(result_middle_6, expected_middle_6, 6) != 0) { failures++; }
    result_middle_6 = explicit_middle_6()
    if (memcmp(result_middle_6, expected_middle_6, 6) != 0) { failures++; }

    // Raw literal Views retain all decoded bytes and their full extent.
    new raw_middle[4] as bytes = "a\0b"
    new raw_expected_middle[4] as bytes = 'a\0b\0'
    if (memcmp(raw_middle, raw_expected_middle, 4) != 0) { failures++; }
    raw_middle = "a\0b"
    if (memcmp(raw_middle, raw_expected_middle, 4) != 0) { failures++; }
    if (memcmp("a\0b", raw_expected_middle, 4) != 0) { failures++; }
    new inferred_middle as cstr = "a\0b"
    if (length("a\0b") != 4 || length(inferred_middle) != 4) { failures++; }

    // trailing NUL shape, destination capacity 1; compare every byte.
    new expected_trailing_1[1] as bytes = '\0'
    new declared_trailing_1[1] as cstr = "ab\0"
    new explicit_trailing_1[1] as bytes %s= "ab\0"
    new assigned_trailing_1[1] as cstr = "zzzzzz"
    assigned_trailing_1 = "ab\0"
    new reassigned_trailing_1[1] as bytes %s= "zzzzzz"
    reassigned_trailing_1 %s= "ab\0"
    if (memcmp(declared_trailing_1, expected_trailing_1, 1) != 0) { failures++; }
    if (memcmp(explicit_trailing_1, expected_trailing_1, 1) != 0) { failures++; }
    if (memcmp(assigned_trailing_1, expected_trailing_1, 1) != 0) { failures++; }
    if (memcmp(reassigned_trailing_1, expected_trailing_1, 1) != 0) { failures++; }
    new result_trailing_1[1] as bytes = named_trailing_1()
    if (memcmp(result_trailing_1, expected_trailing_1, 1) != 0) { failures++; }
    result_trailing_1 = anonymous_trailing_1()
    if (memcmp(result_trailing_1, expected_trailing_1, 1) != 0) { failures++; }
    result_trailing_1 = explicit_trailing_1()
    if (memcmp(result_trailing_1, expected_trailing_1, 1) != 0) { failures++; }

    // trailing NUL shape, destination capacity 3; compare every byte.
    new expected_trailing_3[3] as bytes = 'ab\0'
    new declared_trailing_3[3] as cstr = "ab\0"
    new explicit_trailing_3[3] as bytes %s= "ab\0"
    new assigned_trailing_3[3] as cstr = "zzzzzz"
    assigned_trailing_3 = "ab\0"
    new reassigned_trailing_3[3] as bytes %s= "zzzzzz"
    reassigned_trailing_3 %s= "ab\0"
    if (memcmp(declared_trailing_3, expected_trailing_3, 3) != 0) { failures++; }
    if (memcmp(explicit_trailing_3, expected_trailing_3, 3) != 0) { failures++; }
    if (memcmp(assigned_trailing_3, expected_trailing_3, 3) != 0) { failures++; }
    if (memcmp(reassigned_trailing_3, expected_trailing_3, 3) != 0) { failures++; }
    new result_trailing_3[3] as bytes = named_trailing_3()
    if (memcmp(result_trailing_3, expected_trailing_3, 3) != 0) { failures++; }
    result_trailing_3 = anonymous_trailing_3()
    if (memcmp(result_trailing_3, expected_trailing_3, 3) != 0) { failures++; }
    result_trailing_3 = explicit_trailing_3()
    if (memcmp(result_trailing_3, expected_trailing_3, 3) != 0) { failures++; }

    // trailing NUL shape, destination capacity 4; compare every byte.
    new expected_trailing_4[4] as bytes = 'ab\0\0'
    new declared_trailing_4[4] as cstr = "ab\0"
    new explicit_trailing_4[4] as bytes %s= "ab\0"
    new assigned_trailing_4[4] as cstr = "zzzzzz"
    assigned_trailing_4 = "ab\0"
    new reassigned_trailing_4[4] as bytes %s= "zzzzzz"
    reassigned_trailing_4 %s= "ab\0"
    if (memcmp(declared_trailing_4, expected_trailing_4, 4) != 0) { failures++; }
    if (memcmp(explicit_trailing_4, expected_trailing_4, 4) != 0) { failures++; }
    if (memcmp(assigned_trailing_4, expected_trailing_4, 4) != 0) { failures++; }
    if (memcmp(reassigned_trailing_4, expected_trailing_4, 4) != 0) { failures++; }
    new result_trailing_4[4] as bytes = named_trailing_4()
    if (memcmp(result_trailing_4, expected_trailing_4, 4) != 0) { failures++; }
    result_trailing_4 = anonymous_trailing_4()
    if (memcmp(result_trailing_4, expected_trailing_4, 4) != 0) { failures++; }
    result_trailing_4 = explicit_trailing_4()
    if (memcmp(result_trailing_4, expected_trailing_4, 4) != 0) { failures++; }

    // trailing NUL shape, destination capacity 6; compare every byte.
    new expected_trailing_6[6] as bytes = 'ab\0\0\0\0'
    new declared_trailing_6[6] as cstr = "ab\0"
    new explicit_trailing_6[6] as bytes %s= "ab\0"
    new assigned_trailing_6[6] as cstr = "zzzzzz"
    assigned_trailing_6 = "ab\0"
    new reassigned_trailing_6[6] as bytes %s= "zzzzzz"
    reassigned_trailing_6 %s= "ab\0"
    if (memcmp(declared_trailing_6, expected_trailing_6, 6) != 0) { failures++; }
    if (memcmp(explicit_trailing_6, expected_trailing_6, 6) != 0) { failures++; }
    if (memcmp(assigned_trailing_6, expected_trailing_6, 6) != 0) { failures++; }
    if (memcmp(reassigned_trailing_6, expected_trailing_6, 6) != 0) { failures++; }
    new result_trailing_6[6] as bytes = named_trailing_6()
    if (memcmp(result_trailing_6, expected_trailing_6, 6) != 0) { failures++; }
    result_trailing_6 = anonymous_trailing_6()
    if (memcmp(result_trailing_6, expected_trailing_6, 6) != 0) { failures++; }
    result_trailing_6 = explicit_trailing_6()
    if (memcmp(result_trailing_6, expected_trailing_6, 6) != 0) { failures++; }

    // Raw literal Views retain all decoded bytes and their full extent.
    new raw_trailing[4] as bytes = "ab\0"
    new raw_expected_trailing[4] as bytes = 'ab\0\0'
    if (memcmp(raw_trailing, raw_expected_trailing, 4) != 0) { failures++; }
    raw_trailing = "ab\0"
    if (memcmp(raw_trailing, raw_expected_trailing, 4) != 0) { failures++; }
    if (memcmp("ab\0", raw_expected_trailing, 4) != 0) { failures++; }
    new inferred_trailing as cstr = "ab\0"
    if (length("ab\0") != 4 || length(inferred_trailing) != 4) { failures++; }

    // empty NUL shape, destination capacity 1; compare every byte.
    new expected_empty_1[1] as bytes = '\0'
    new declared_empty_1[1] as cstr = ""
    new explicit_empty_1[1] as bytes %s= ""
    new assigned_empty_1[1] as cstr = "zzzzzz"
    assigned_empty_1 = ""
    new reassigned_empty_1[1] as bytes %s= "zzzzzz"
    reassigned_empty_1 %s= ""
    if (memcmp(declared_empty_1, expected_empty_1, 1) != 0) { failures++; }
    if (memcmp(explicit_empty_1, expected_empty_1, 1) != 0) { failures++; }
    if (memcmp(assigned_empty_1, expected_empty_1, 1) != 0) { failures++; }
    if (memcmp(reassigned_empty_1, expected_empty_1, 1) != 0) { failures++; }
    new result_empty_1[1] as bytes = named_empty_1()
    if (memcmp(result_empty_1, expected_empty_1, 1) != 0) { failures++; }
    result_empty_1 = anonymous_empty_1()
    if (memcmp(result_empty_1, expected_empty_1, 1) != 0) { failures++; }
    result_empty_1 = explicit_empty_1()
    if (memcmp(result_empty_1, expected_empty_1, 1) != 0) { failures++; }

    // empty NUL shape, destination capacity 4; compare every byte.
    new expected_empty_4[4] as bytes = '\0\0\0\0'
    new declared_empty_4[4] as cstr = ""
    new explicit_empty_4[4] as bytes %s= ""
    new assigned_empty_4[4] as cstr = "zzzzzz"
    assigned_empty_4 = ""
    new reassigned_empty_4[4] as bytes %s= "zzzzzz"
    reassigned_empty_4 %s= ""
    if (memcmp(declared_empty_4, expected_empty_4, 4) != 0) { failures++; }
    if (memcmp(explicit_empty_4, expected_empty_4, 4) != 0) { failures++; }
    if (memcmp(assigned_empty_4, expected_empty_4, 4) != 0) { failures++; }
    if (memcmp(reassigned_empty_4, expected_empty_4, 4) != 0) { failures++; }
    new result_empty_4[4] as bytes = named_empty_4()
    if (memcmp(result_empty_4, expected_empty_4, 4) != 0) { failures++; }
    result_empty_4 = anonymous_empty_4()
    if (memcmp(result_empty_4, expected_empty_4, 4) != 0) { failures++; }
    result_empty_4 = explicit_empty_4()
    if (memcmp(result_empty_4, expected_empty_4, 4) != 0) { failures++; }

    // Raw literal Views retain all decoded bytes and their full extent.
    new raw_empty[1] as bytes = ""
    new raw_expected_empty[1] as bytes = '\0'
    if (memcmp(raw_empty, raw_expected_empty, 1) != 0) { failures++; }
    raw_empty = ""
    if (memcmp(raw_empty, raw_expected_empty, 1) != 0) { failures++; }
    if (memcmp("", raw_expected_empty, 1) != 0) { failures++; }
    new inferred_empty as cstr = ""
    if (length("") != 1 || length(inferred_empty) != 1) { failures++; }

    // plain NUL shape, destination capacity 1; compare every byte.
    new expected_plain_1[1] as bytes = '\0'
    new declared_plain_1[1] as cstr = "abcd"
    new explicit_plain_1[1] as bytes %s= "abcd"
    new assigned_plain_1[1] as cstr = "zzzzzz"
    assigned_plain_1 = "abcd"
    new reassigned_plain_1[1] as bytes %s= "zzzzzz"
    reassigned_plain_1 %s= "abcd"
    if (memcmp(declared_plain_1, expected_plain_1, 1) != 0) { failures++; }
    if (memcmp(explicit_plain_1, expected_plain_1, 1) != 0) { failures++; }
    if (memcmp(assigned_plain_1, expected_plain_1, 1) != 0) { failures++; }
    if (memcmp(reassigned_plain_1, expected_plain_1, 1) != 0) { failures++; }
    new result_plain_1[1] as bytes = named_plain_1()
    if (memcmp(result_plain_1, expected_plain_1, 1) != 0) { failures++; }
    result_plain_1 = anonymous_plain_1()
    if (memcmp(result_plain_1, expected_plain_1, 1) != 0) { failures++; }
    result_plain_1 = explicit_plain_1()
    if (memcmp(result_plain_1, expected_plain_1, 1) != 0) { failures++; }

    // plain NUL shape, destination capacity 3; compare every byte.
    new expected_plain_3[3] as bytes = 'ab\0'
    new declared_plain_3[3] as cstr = "abcd"
    new explicit_plain_3[3] as bytes %s= "abcd"
    new assigned_plain_3[3] as cstr = "zzzzzz"
    assigned_plain_3 = "abcd"
    new reassigned_plain_3[3] as bytes %s= "zzzzzz"
    reassigned_plain_3 %s= "abcd"
    if (memcmp(declared_plain_3, expected_plain_3, 3) != 0) { failures++; }
    if (memcmp(explicit_plain_3, expected_plain_3, 3) != 0) { failures++; }
    if (memcmp(assigned_plain_3, expected_plain_3, 3) != 0) { failures++; }
    if (memcmp(reassigned_plain_3, expected_plain_3, 3) != 0) { failures++; }
    new result_plain_3[3] as bytes = named_plain_3()
    if (memcmp(result_plain_3, expected_plain_3, 3) != 0) { failures++; }
    result_plain_3 = anonymous_plain_3()
    if (memcmp(result_plain_3, expected_plain_3, 3) != 0) { failures++; }
    result_plain_3 = explicit_plain_3()
    if (memcmp(result_plain_3, expected_plain_3, 3) != 0) { failures++; }

    // plain NUL shape, destination capacity 4; compare every byte.
    new expected_plain_4[4] as bytes = 'abc\0'
    new declared_plain_4[4] as cstr = "abcd"
    new explicit_plain_4[4] as bytes %s= "abcd"
    new assigned_plain_4[4] as cstr = "zzzzzz"
    assigned_plain_4 = "abcd"
    new reassigned_plain_4[4] as bytes %s= "zzzzzz"
    reassigned_plain_4 %s= "abcd"
    if (memcmp(declared_plain_4, expected_plain_4, 4) != 0) { failures++; }
    if (memcmp(explicit_plain_4, expected_plain_4, 4) != 0) { failures++; }
    if (memcmp(assigned_plain_4, expected_plain_4, 4) != 0) { failures++; }
    if (memcmp(reassigned_plain_4, expected_plain_4, 4) != 0) { failures++; }
    new result_plain_4[4] as bytes = named_plain_4()
    if (memcmp(result_plain_4, expected_plain_4, 4) != 0) { failures++; }
    result_plain_4 = anonymous_plain_4()
    if (memcmp(result_plain_4, expected_plain_4, 4) != 0) { failures++; }
    result_plain_4 = explicit_plain_4()
    if (memcmp(result_plain_4, expected_plain_4, 4) != 0) { failures++; }

    // plain NUL shape, destination capacity 6; compare every byte.
    new expected_plain_6[6] as bytes = 'abcd\0\0'
    new declared_plain_6[6] as cstr = "abcd"
    new explicit_plain_6[6] as bytes %s= "abcd"
    new assigned_plain_6[6] as cstr = "zzzzzz"
    assigned_plain_6 = "abcd"
    new reassigned_plain_6[6] as bytes %s= "zzzzzz"
    reassigned_plain_6 %s= "abcd"
    if (memcmp(declared_plain_6, expected_plain_6, 6) != 0) { failures++; }
    if (memcmp(explicit_plain_6, expected_plain_6, 6) != 0) { failures++; }
    if (memcmp(assigned_plain_6, expected_plain_6, 6) != 0) { failures++; }
    if (memcmp(reassigned_plain_6, expected_plain_6, 6) != 0) { failures++; }
    new result_plain_6[6] as bytes = named_plain_6()
    if (memcmp(result_plain_6, expected_plain_6, 6) != 0) { failures++; }
    result_plain_6 = anonymous_plain_6()
    if (memcmp(result_plain_6, expected_plain_6, 6) != 0) { failures++; }
    result_plain_6 = explicit_plain_6()
    if (memcmp(result_plain_6, expected_plain_6, 6) != 0) { failures++; }

    // Raw literal Views retain all decoded bytes and their full extent.
    new raw_plain[5] as bytes = "abcd"
    new raw_expected_plain[5] as bytes = 'abcd\0'
    if (memcmp(raw_plain, raw_expected_plain, 5) != 0) { failures++; }
    raw_plain = "abcd"
    if (memcmp(raw_plain, raw_expected_plain, 5) != 0) { failures++; }
    if (memcmp("abcd", raw_expected_plain, 5) != 0) { failures++; }
    new inferred_plain as cstr = "abcd"
    if (length("abcd") != 5 || length(inferred_plain) != 5) { failures++; }

    if (calls != 54) { failures++; }
    new padded[4] as bytes = 'a\0\0\0'
    new hex_zero[4] as cstr = "a\x00b"
    new unicode_zero[4] as bytes %s= "a\u0000b"
    if (memcmp(hex_zero, padded, 4) != 0 || memcmp(unicode_zero, padded, 4) != 0) { failures++; }
    new raw_hex[4] as bytes = "a\x00b"
    new raw_unicode[4] as bytes = "a\u0000b"
    if (memcmp(raw_hex, raw_expected_middle, 4) != 0 || memcmp(raw_unicode, raw_expected_middle, 4) != 0) { failures++; }
    if (memcmp(global_literal, padded, 4) != 0) { failures++; }
    new cached[4] as bytes = cached_literal()
    if (memcmp(cached, padded, 4) != 0) { failures++; }
    new framed as Framed
    framed.head = 17
    framed.tail = 19
    framed.text = "a\0b"
    if (memcmp(framed.text, padded, 4) != 0 || framed.head != 17 || framed.tail != 19) { failures++; }
    framed.text %s= "\0ab"
    new zero[4] as bytes = '\0\0\0\0'
    if (memcmp(framed.text, zero, 4) != 0 || framed.head != 17 || framed.tail != 19) { failures++; }
    new snapshot[4] as bytes = (global_literal = "a\0b")
    global_literal = "xyz"
    if (memcmp(snapshot, padded, 4) != 0 || length(global_literal = "a\0b") != 4) { failures++; }
    printf("cstr literal padding %d\n", failures)
    return failures
}
