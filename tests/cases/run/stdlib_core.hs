$include <stdlib.hsh>
$include <string.hsh>
$include <ctype.hsh>

func main() {
    new ptr = calloc(1, 8)
    memset(ptr, 0, 8)
    [1]*ptr = 'A'
    [1]*(ptr? + 1) = 'B'
    new text[3] as cstr = "AB"
    new len = strlen(&text)
    new cmp[4] = memcmp(ptr, ptr, 2)
    new match = strchr(&text, 'B')
    new text_base as addr = &text
    new expected_match as addr = text_base? + 1
    new swapped[2] = byte_swap(0x1234)
    new raw[1] = resize_bytes(swapped, 1)
    new upper[1] = to_upper('a')
    free(ptr)
    new ok[1] = len == 2 && cmp == 0 && match == expected_match && upper == 'A'
    if (ok) {
        return 0
    } else {
        return 1
    }
}
