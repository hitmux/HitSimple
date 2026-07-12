$include <stdlib.hsh>
$include <string.hsh>

func main() {
    new source[4] = 0x03020100
    new destination[4]
    new heap = calloc(1, 4)
    new copied = memcpy(destination, source, 4)
    new compared = memcmp(destination, source, 4)
    new text_length = strlen("ok")
    free(heap)
    return compared
}
