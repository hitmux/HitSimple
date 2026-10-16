$include <stdlib.hsh>
func opaque(p as addr) -> addr { return p; }
func main() -> i32 {
    new memory as addr = calloc(1, 1)
    new value[16] as bytes
    new output[16] as bytes
    value &= opaque(memory)
    output = value
    return 0
}
