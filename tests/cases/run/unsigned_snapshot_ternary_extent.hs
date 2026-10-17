$include <stdlib.hsh>
func probe(pointer as addr, select as bool) -> u64 {
    new rebound[8] as cstr
    rebound &= pointer
    return length((select ? rebound : "abcdefg")?)
}
func main() -> i32 { new raw[1] as bytes = '\0'; new ignored = probe(&raw, true); return 0; }
