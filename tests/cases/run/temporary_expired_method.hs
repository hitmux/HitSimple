$include <stdlib.hsh>
$include <string.hsh>
func opaque(value as addr) -> addr { return value; }
template Text { data[4] as bytes; }
impl Text {
    func copy(self as Text) -> Text { return self; }
    op + (left as Text, right as Text) -> Text { return left; }
}
func main() -> i32 {
    new text[4] as cstr = "abc"
    new p as addr = &text
    new source as Text
    source.data = "abc" as bytes
    p = opaque(strchr(source.copy() as cstr, 'a'))
    return ([1]*p) - 97
}
