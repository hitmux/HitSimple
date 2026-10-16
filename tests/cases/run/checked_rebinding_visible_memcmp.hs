$include <string.hsh>
func amount() -> u64 { return 2; }
func main() -> i32 {
    new small[1] as bytes; new large[8] as bytes
    small &= &large
    new observed as i32 = memcmp(large, small, amount())
    return 0
}
