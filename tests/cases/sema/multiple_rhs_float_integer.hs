func source() -> f64 { return 2.5; }
func main() -> i32 {
    new first as u64
    new second as u64
    first, second = 1, source()
    return 0
}
