func null_address() -> addr { return 0; }
func main() -> i32 {
    new value as f64 = 1.0
    value &= null_address()
    value = 2.0
    return 0
}
