$include <stdlib.hsh>

func main() -> i32 {
    new local[1] as bytes
    new pointer = realloc(&local, 2)
    return 0
}
