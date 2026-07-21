$include <stdlib.hsh>

new pointer as addr = alloc(1)

func main() -> i32 {
    free(pointer)
    free(pointer)
    return 0
}
