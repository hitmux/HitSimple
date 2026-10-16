func main() -> i32 {
    new small[2] as cstr; new large[8] as cstr = "abcdefg"
    small &= &large
    new copied[2] as cstr = small
    return 0
}
