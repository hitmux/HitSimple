func escaped_address() -> [8] {
    new value[4]
    value = 42
    return &value
}

func main() {
    new ptr[8]
    ptr = escaped_address()
    return [4]*ptr
}
