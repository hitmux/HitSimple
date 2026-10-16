$include <stdlib.hsh>
template Record { prefix[12] as bytes; tail[4] as i32; }
impl Record {
    op = (dst as Record, src as Record) -> Record {
        dst.tail = 7
        return dst
    }
}
func opaque(p as addr) -> addr { return p; }
func main() -> i32 {
    new memory as addr = calloc(1, 4)
    new destination as Record
    new source as Record
    destination &= opaque(memory)
    destination = source
    return 0
}
