$include <stdlib.hsh>

func main() -> i32 {
    new count as u64 = 18446744073709551615
    new size as u64 = 2
    new ptr as addr = calloc(count, size)
    return 0
}
