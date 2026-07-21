func condition() -> bool {
    return true
}

func initial() -> addr {
    return 0
}

func main() -> i32 {
    new p as addr = initial()
    while (condition()) {
        new value[1] as bytes = 'X'
        p = &value
        break
    }
    return [1]*p
}
