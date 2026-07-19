$include <stdlib.hsh>
$include <string.hsh>

func main() -> i32 {
    new ptr as addr = calloc(0, 1)
    memset(ptr, 0, 0)
    new copied as addr = memcpy(ptr, ptr, 0)
    new moved as addr = memmove(ptr, ptr, 0)
    new compared as i32 = memcmp(ptr, ptr, 0)
    return compared
}
