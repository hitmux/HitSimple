$include <stdlib.hsh>
func probe(p as addr) -> u64 { new view[8] as cstr; view &= p; return length(view?); }
func main() -> i32 { new raw[1] as bytes = '\0'; new ignored = probe(&raw); return 0; }
