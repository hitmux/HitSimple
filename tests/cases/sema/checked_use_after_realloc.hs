$include <stdlib.hsh>

func main() -> i32 {
    new pointer = alloc(1)
    new alias as addr = pointer
    pointer = realloc(pointer, 2)
    return [1]*alias
}
