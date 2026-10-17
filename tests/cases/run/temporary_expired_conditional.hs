$include <stdlib.hsh>
$include <string.hsh>
func opaque(value as addr) -> addr { return value; }
func main() -> i32 {
    new text[4] as cstr = "abc"
    new p as addr = &text
    p = opaque(strchr((true ? "abc" : "xyz"), 'a'))
    return ([1]*p) - 97
}
