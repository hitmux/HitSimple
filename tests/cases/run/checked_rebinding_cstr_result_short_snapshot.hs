func opaque(value as addr) -> addr { return value; }
func main() -> i32 {
    new name[8] as cstr = "old"
    new target[2] as cstr = "x"
    new result[1] as cstr = (name &= opaque(&target))
    return 0
}
