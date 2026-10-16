$include <stdlib.hsh>
func probe(p as addr) -> u64 { return length(([1]*p)?); }
func main() -> i32 { new pointer as addr = 0; if (true) { new local as u8 = 7; pointer = &local; }; new ignored = probe(pointer); return 0; }
