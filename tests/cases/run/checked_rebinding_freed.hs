$include <stdlib.hsh>
func opaque(p as addr) -> addr { return p; }
func main() -> i32 {
    new memory as addr = calloc(4, 1)
    new value as i32 = 1
    value &= opaque(memory)
    free(memory)
    return value
}
