$include <stdlib.hsh>

func main() -> i32 {
    new pointer = alloc(2)
    new interior as addr = pointer? + 1
    free(interior)
    return 0
}
