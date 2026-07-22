func main() -> i32 {
    new pointer as addr = alloc(1)
    if ([1]*pointer) {
        free(pointer)
        return 0
    }
    free(pointer)
    return 0
}
