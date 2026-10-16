func main() -> i32 {
    new name[2] as cstr = "x"
    new target[8] as cstr = "abcdefg"
    new result[1] as cstr = (name &= &target)
    return 0
}
