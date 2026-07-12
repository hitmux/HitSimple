$include <stdlib.hsh>
$include <string.hsh>
$include <stdio.hsh>
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
    new upper[1] = to_upper('a')

    if (match != expected_match) {
        return 1
    }
    printf("%d\n", len)
    free(ptr)

    if (cmp == 0 && match == expected_match && upper == 'A') {
        return 0
    } else {
        return 1
    }
}
