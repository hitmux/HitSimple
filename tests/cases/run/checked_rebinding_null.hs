func null_address() -> addr { return 0; }
func main() -> i32 {
    new value as i32 = 1
    value &= null_address()
    return value
}
