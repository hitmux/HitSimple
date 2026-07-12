$include <stdlib.hsh>

func main() {
    new ptr = alloc(4)
    free(ptr)
    free(ptr)
    return 0
}
