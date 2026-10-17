$include <stdlib.hsh>
$include <string.hsh>
func main() -> i32 {
    new p as addr = strchr("abc", 'a')
    return length(resize_bytes(([4]*p)?, 0))
}
