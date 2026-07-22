func runtime_negative_exponent() -> i32 {
    return -1
}

func main() -> i32 {
    new exponent as i32 = runtime_negative_exponent()
    return 2 ** exponent
}
