$include <stdlib.hsh>
$include <string.hsh>
func opaque(value as addr) -> addr { return value; }
func main() -> i32 {
    new text[4] as cstr = "abc"
    new p as addr = &text
    try { throw opaque(strchr("abc", 'a')); }
    catch (error as addr) { return ([1]*error) - 97; }
    return ([1]*p) - 97
}
