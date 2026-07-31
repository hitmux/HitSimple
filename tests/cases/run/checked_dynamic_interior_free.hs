$include <stdlib.hsh>

func dynamic_offset() -> u64 {
    return 1
}

func main() {
    new pointer as addr = alloc(2)
    new interior as addr = pointer? + dynamic_offset()?
    free(interior)
    return 0
}
