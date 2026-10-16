func text() -> out[1] as cstr {
    new name[2] as cstr = "x"
    new target[8] as cstr = "abcdefg"
    return (name &= &target)
}
func main() -> i32 { new result[1] as cstr = text(); return 0; }
