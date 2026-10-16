$include <string.hsh>
func amount() -> u64 { return 2; }
func main() -> i32 {
    new small[1] as bytes; new large[8] as bytes; new output[8] as bytes
    small &= &large
    _ = memcpy(output, small, amount())
    return 0
}
