func main() {
    new bytes[8]
    new value[4]
    new pointer as addr = &value
    bytes[0:4] = 42
    value = bytes[0:+4]
    [4]*pointer = [4]*pointer + 1
    return value - 43
}
