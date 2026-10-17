$include <stdlib.hsh>
$include <string.hsh>
$include <stdio.hsh>
new global_text[4] as cstr = "abc"
func opaque(value as addr) -> addr { return value; }
func nested_read(value as addr) -> i32 {
    new size as u64 = strlen("callee")
    if (size != 6) { return 1; }
    return ([1]*value) - 97
}
func named_return() -> (text[4] as cstr) { text = "abc"; return; }
func static_read() -> i32 {
    static text[4] as cstr = "abc"
    new pointer as addr = strchr(text, 'a')
    new size as u64 = strlen("later")
    return ([1]*pointer) - 97
}
template Text { data[4] as bytes; }
impl Text {
    func copy(self as Text) -> Text { return self; }
    op + (left as Text, right as Text) -> Text { return left; }
}
func main() -> i32 {
    new failures as i32 = 0
    new text[4] as cstr = "abc"
    new source as Text
    source.data = "abc" as bytes
    new width as u64 = 4
    failures = failures + nested_read(strchr("abc", 'a'))
    failures = failures + nested_read(strchr(resize_bytes("abc", 4) as cstr, 'a'))
    failures = failures + nested_read(strchr(resize_bytes("abc", width) as cstr, 'a'))
    failures = failures + nested_read(strchr((text?) as cstr, 'a'))
    failures = failures + nested_read(strchr((text = "abc"), 'a'))
    failures = failures + nested_read(strchr((true ? "abc" : "xyz"), 'a'))
    failures = failures + nested_read(strchr(byte_swap(byte_swap(text)) as cstr, 'a'))
    failures = failures + nested_read(strchr(source.copy() as cstr, 'a'))
    failures = failures + nested_read(strchr((source + source) as cstr, 'a'))
    new copied[4] as cstr = named_return()
    new pointer as addr = strchr(copied, 'a')
    new other as addr = strchr(global_text, 'a')
    failures = failures + static_read()
    failures = failures + static_read()
    failures = failures + nested_read(pointer) + nested_read(other)
    new heap as addr = alloc(4)
    memcpy(heap, "abc", 4)
    failures = failures + nested_read(heap)
    free(heap)
    new left[4] as cstr = "abc", right[4] as cstr = "xyz"
    left, right = right, left
    if (strcmp(left, "xyz") != 0 || strcmp(right, "abc") != 0) { failures++; }
    new index as i32 = 0
    while (index < 1024 && strlen("abc") == 3) {
        width = index % 2
        if (length(resize_bytes("abc", width)) != width) { failures++; }
        index++
        if (index < 1024) { continue; }
    }
    for (new j as i32 = 0; j < 3 && strlen("abc") == 3; j++, width = strlen("abc")) {
        if (j == 2) { break; }
    }
    if (strlen("abc") == 3 && nested_read(strchr("abc", 'a')) == 0) { failures = failures; }
    else { failures++; }
    goto after
    failures++
    after: failures = failures
    try { throw "abc"; }
    catch (error[4] as cstr) { if (strcmp(error, "abc") != 0) { failures++; } }
    // A later repair must replace stale expiry facts on each actual exit edge.
    pointer = strchr("abc", 'a')
    for (;;) { pointer = strchr(text, 'a'); break; }
    failures = failures + nested_read(pointer)
    pointer = strchr("abc", 'a')
    new more as i32 = 1
    while (more) { pointer = strchr(text, 'a'); more = 0; }
    failures = failures + nested_read(pointer)
    pointer = strchr("abc", 'a')
    try { pointer = strchr(text, 'a'); throw 0; }
    catch (error as i32) { failures = failures + nested_read(pointer); }
    pointer = strchr("abc", 'a')
    try { if (more == 0) { pointer = strchr(text, 'a'); throw 0; } }
    catch (error as i32) { failures = failures + nested_read(pointer); }
    printf("temporary lifetimes %d\n", failures)
    return failures
}
