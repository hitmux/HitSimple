$include <stdlib.hsh>

func runtime_extent() -> i32 {
    return 4
}

func main() {
    new extent as i32 = runtime_extent()
    new ptr = alloc(extent)
    [1]*(ptr? + 4) = 1
    free(ptr)
    return 0
}
