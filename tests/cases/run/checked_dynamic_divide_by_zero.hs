func runtime_zero() -> i32 {
    return 0
}

func main() -> i32 {
    new divisor as i32 = runtime_zero()
    return 7 / divisor
}
