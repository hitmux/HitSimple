$include <stdlib.hsh>
$include <string.hsh>
func main() -> i32 {
    new p as addr = strchr(resize_bytes("abc", 4) as cstr, 'a')
    return ([1]*p) - 97
}
