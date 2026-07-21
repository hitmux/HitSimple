$include <stdlib.hsh>

func main() -> i32 {
    new pointer as addr = allocate()
    free(pointer)
    if ([1]*pointer) {
        return 1
    }
    return 0
}

func allocate() -> addr {
    return alloc(1)
}
