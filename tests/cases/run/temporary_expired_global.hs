$include <stdlib.hsh>
$include <string.hsh>
func opaque(value as addr) -> addr { return value; }
new escaped_global as addr = opaque(strchr("abc", 'a'))
func main() -> i32 {
    new text[4] as cstr = "abc"
    new p as addr = &text
    p = escaped_global
    return ([1]*p) - 97
}
