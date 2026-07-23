func main() {
    new buffer[4] as bytes
    new base as addr = &buffer
    new offset as u64 = 4
    new target as addr = base? + offset?
    [1]*target = 1
    return 0
}
