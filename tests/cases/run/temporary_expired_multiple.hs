$include <stdlib.hsh>
$include <string.hsh>
func opaque(value as addr) -> addr { return value; }
func main() -> i32 {
    new text[4] as cstr = "abc"
    new p as addr = &text
    new q as addr = &text
    p, q = opaque(strchr("abc", 'a')), &text
    return ([1]*p) - 97
}
