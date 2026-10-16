$include <stdio.hsh>
func amount() -> u64 { return 2; }
func main() -> i32 {
    new small[2] as cstr; new large[8] as cstr = "abcdefg"
    small &= &large
    printf("%s", small)
    return 0
}
