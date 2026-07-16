func load_byte(src[P] as addr) -> [1] effects(read(src, 1), nothrow) {
    return [1]*src
}

func main() -> i32 {
    return 0
}
