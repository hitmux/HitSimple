func runtime_negative_shift() -> i32 {
    return -1
}

func main() -> i32 {
    new shift as i32 = runtime_negative_shift()
    return 1 << shift
}
