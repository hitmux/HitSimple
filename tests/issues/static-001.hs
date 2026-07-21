func next() -> i32 {
    static n as i32 = 0
    n++
    return n
}

func main() -> i32 {
    return next() * 10 + next() - 12
}
