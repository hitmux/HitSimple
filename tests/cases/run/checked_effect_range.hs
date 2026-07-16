func store_byte(dst[P] as addr) -> () effects(write(dst, 1), nothrow) {
    [1]*dst = 7
}

func main() -> i32 {
    new byte[1]
    store_byte(&byte)
    return 0
}
