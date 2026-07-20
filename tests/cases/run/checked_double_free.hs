$include <stdlib.hsh>

func main() {
    new ptr = allocate()
    free(ptr)
    free(ptr)
    return 0
}

func allocate() -> addr {
    return alloc(4)
}
