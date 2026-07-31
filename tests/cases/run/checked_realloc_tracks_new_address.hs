$include <stdlib.hsh>

func dynamic_size() -> u64 {
    return 1048576
}

func main() {
    new pointer as addr = alloc(64)
    new blocker as addr = alloc(64)
    pointer = realloc(pointer, dynamic_size())
    [1]*pointer = 1
    free(pointer)
    free(blocker)
    return 0
}
