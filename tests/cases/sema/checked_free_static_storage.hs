$include <stdlib.hsh>

func main() -> i32 {
    new local[1] as bytes
    free(&local)
    return 0
}
