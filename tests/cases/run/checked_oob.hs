$include <stdlib.hsh>

func main() {
    new ptr = alloc(4)
    [1]*(ptr + 4) = 1
    free(ptr)
    return 0
}
