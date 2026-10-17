$include <stdlib.hsh>
func probe(pointer as addr, select as bool) -> u64 {
    return length((select ? (([4]*pointer) as bytes) : "a\0b" as bytes)?)
}
func main() -> i32 {
    new pointer as addr = 0
    if (true) { new local[4] as bytes = "a\0b"; pointer = &local; }
    new ignored = probe(pointer, true)
    return 0
}
