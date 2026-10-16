$include <string.hsh>
func amount() -> u64 { return 2; }
func main() -> i32 {
    new small[1] as cstr; new large[8] as cstr = ""
    small &= &large
    _ = strncpy(small, "abc", amount())
    return 0
}
