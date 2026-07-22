func helper(value as i32) -> i32 {
    return value %d+ 1
}

func main() {
    new x[1]
    x %d= helper(41)
    return x - 42
}
