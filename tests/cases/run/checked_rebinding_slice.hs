func position() -> u64 { return 2; }
func main() -> i32 {
    new small[2] as bytes
    new large[8] as bytes
    small &= &large
    new copy[2] as bytes = small[position():+2]; return 0
}
