$include <stdlib.hsh>
$include <string.hsh>
func opaque(value as addr) -> addr { return value; }
func main() -> i32 {
    new text[4] as cstr = "abc"
    new p as addr = &text
    if ((p = opaque(strchr("abc", 'a')))) { return ([1]*p) - 97; }
    return ([1]*p) - 97
}
