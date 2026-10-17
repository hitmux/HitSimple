$include <stdlib.hsh>
func probe(n as u64) -> u64 { new source[3] as bytes = 'abc'; return (resize_bytes(source, n)? as u64); }
func main() -> i32 { new ignored = probe(3); return 0; }
