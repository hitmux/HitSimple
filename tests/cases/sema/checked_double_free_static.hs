$include <stdlib.hsh>

func main() -> i32 {
    new pointer = alloc(1)
    free(pointer)
    free(pointer)
    return 0
}
