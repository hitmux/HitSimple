$include <stdlib.hsh>

func main() {
    new ptr = alloc(4)
    free(ptr)
    return [1]*ptr
}
