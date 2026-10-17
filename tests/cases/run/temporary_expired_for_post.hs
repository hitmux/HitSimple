$include <stdlib.hsh>
$include <string.hsh>
func opaque(value as addr) -> addr { return value; }
func main() -> i32 {
    new text[4] as cstr = "abc"
    new p as addr = &text
    new i as i32 = 0
    for (; i < 1; p = opaque(strchr("abc", 'a')), i++) { }
    return ([1]*p) - 97
}
