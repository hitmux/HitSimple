$include <stdlib.hsh>
$include <string.hsh>

func main() -> i32 {
    new size as u64 = 64
    new p as addr = alloc(size)
    new null_addr as addr = 0

    if (p == null_addr) {
        return 1
    }

    [64]*p as bytes = resize_bytes(0, 64) as bytes
    free(p)
    return 0
}
