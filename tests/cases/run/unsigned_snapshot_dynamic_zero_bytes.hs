$include <stdlib.hsh>
func probe(n as u64) -> u64 { new source[3] as bytes = 'abc'; return length(resize_bytes(source, n)? as bytes); }
func main() -> i32 { new ignored = probe(0); return 0; }
