$include <stdlib.hsh>
func requested() -> u64 { return 3; }
func main() -> i32 {
    new source as u64 = 1
    new size = length(resize_bytes(source, requested()) as u64)
    return 0
}
