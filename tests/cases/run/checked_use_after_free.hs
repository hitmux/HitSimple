$include <stdlib.hsh>

func main() {
    new ptr = allocate()
    free(ptr)
    return [1]*ptr
}

func allocate() -> addr {
    return alloc(4)
}
