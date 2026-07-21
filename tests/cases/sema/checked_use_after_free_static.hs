$include <stdlib.hsh>

func main() -> i32 {
    new pointer = alloc(1)
    free(pointer)
    return [1]*pointer
}
