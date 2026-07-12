func tick() -> [4] {
    static value[4]
    new ptr[8]
    ptr &= &value
    [4]*ptr = [4]*ptr + 1
    return [4]*ptr
}

func main() {
    return tick() + tick() - 3
}
