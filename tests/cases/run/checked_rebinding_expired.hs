func expired() -> addr { new value as i32 = 9; return &value; }
func main() -> i32 {
    new value as i32 = 1
    value &= expired()
    return value
}
