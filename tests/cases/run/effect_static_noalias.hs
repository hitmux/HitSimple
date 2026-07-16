func copy_byte(dst[P] as addr, src[P] as addr) -> () effects(read(src, 1), write(dst, 1), noalias(dst, src), nothrow) {
    [1]*dst = [1]*src
}

func main() -> i32 {
    new data[1]
    copy_byte(&data, &data)
    return 0
}
