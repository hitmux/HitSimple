$include <stdlib.hsh>
$include <string.hsh>
func opaque(value as addr) -> addr { return value; }
func main() -> i32 {
    new p as addr = opaque(strchr("abc", 'a'))
    return length(resize_bytes(byte_swap(([4]*p)?), 1))
}
