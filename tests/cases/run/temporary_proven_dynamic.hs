$include <stdlib.hsh>
$include <string.hsh>
func main() -> i32 {
    new p as addr = strchr(resize_bytes("abc", length("abc")) as cstr, 'a')
    return ([1]*p) - 97
}
