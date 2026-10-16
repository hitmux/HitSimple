func position() -> u64 { return 2; }
func main() -> i32 {
    new small[1] as bytes
    new large[8] as bytes
    small &= &large
    return small[position()]
}
