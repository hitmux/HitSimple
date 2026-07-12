new global_value[4]

func main() {
    new ptr[8]
    global_value = 41
    ptr &= &global_value
    [4]*ptr = [4]*ptr + 1
    return [4]*ptr - 42
}
