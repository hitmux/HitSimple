$include <stdio.hsh>
func amount() -> u64 { return 2; }
func main() -> i32 {
    new small[1] as bytes; new large[8] as bytes
    small &= &large
    new file as handle = fopen("rebind-extent.dat", "wb+")
    _ = fwrite(small, 1, amount(), file)
    return 0
}
