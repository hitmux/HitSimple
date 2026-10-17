$include <stdlib.hsh>
$include <string.hsh>
func opaque(value as addr) -> addr { return value; }
func main() -> i32 {
    new text[4] as cstr = "abc"
    new p as addr = &text
    new width as u64 = 4
    p = opaque(strchr(resize_bytes("abc", width) as cstr, 'a'))
    return ([1]*p) - 97
}
