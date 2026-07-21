$include <stdlib.hsh>

func main() -> i32 {
    new pointer as addr = allocate()
    free(pointer)
    new target[1] as bytes
    target = [1]*pointer
    return 0
}

func allocate() -> addr {
    return alloc(1)
}
