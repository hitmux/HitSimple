$include <string.hsh>

func main() -> i32 {
    new text[2] as cstr
    text[0] = 'a'
    text[1] = 'b'
    new length as u64 = strlen(text)
    return 0
}
