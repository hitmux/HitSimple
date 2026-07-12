new escaped[8]

func escape_without_return() {
    new value[4]
    value = 42
    escaped &= &value
}

func main() {
    escape_without_return()
    return [4]*escaped
}
