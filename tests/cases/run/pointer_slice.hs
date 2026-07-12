func main() {
    new bytes[8]
    new value[4]
    new ptr[8]
    bytes[0:4] = 42
    value = bytes[0:+4]
    ptr &= &value
    [4]*ptr = [4]*ptr + 1
    return value - 43
}
